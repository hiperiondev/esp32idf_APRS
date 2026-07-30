.. _it-filtering:

==========
Filtraggio
==========

Il firmware applica vari filtri indipendenti e componibili per decidere quali
pacchetti attraversano tra RF e APRS-IS. Vivono principalmente in
``main/aprs_filter.c`` e sono usati sia dall'IGate (``components/igate/igate.c``)
sia dal gestore INET→RF (``main/aprs_service.c``). Tutti quanti **si compongono
con semantica AND**: un pacchetto deve superare ogni filtro che si applica alla
sua direzione.

.. important::

   Questi filtri locali sono interamente distinti dalla **stringa di filtro lato
   server APRS-IS** (``g_config.aprs_filter``), che è testo libero inoltrato alla
   lettera nella riga di login e applicato dal server APRS-IS a ciò che invia
   *verso* il client. I filtri locali qui sotto decidono cosa il client spinge
   *verso l'esterno*, e cosa ritrasmette in RF.

Classificazione per tipo di payload
===================================

``aprs_filter_classify_tnc2()`` / ``aprs_filter_classify_info()`` decidono a
quale singolo bit ``IGATE_FILT_*`` appartiene un pacchetto, lavorando
sull'identificatore di tipo di dato APRS (DTI) e — dove il DTI da solo è ambiguo
— sul simbolo che il report porta (``_`` → meteo, ``/N`` → boa):

.. list-table::
   :header-rows: 1
   :widths: 30 20 50

   * - Tipo
     - Bit
     - Note
   * - Messaggio
     - ``1<<0``
     - messaggi APRS
   * - Stato
     - ``1<<1``
     - report di stato
   * - Telemetria
     - ``1<<2``
     - report di telemetria
   * - Meteo
     - ``1<<3``
     - report di posizione con simbolo ``_``
   * - Oggetto
     - ``1<<4``
     -
   * - Item
     - ``1<<5``
     -
   * - Query
     - ``1<<6``
     -
   * - Boa
     - ``1<<7``
     - report di posizione con simbolo ``/N``
   * - Posizione
     - ``1<<8``
     - report di posizione semplice

Un payload non classificabile — il traffico di terze parti (``}``) su tutti —
classifica come ``0``, e ``aprs_filter_pass()`` non lascia mai passare ``0``:
sconosciuto significa "non ritrasmettere". Lo stesso vale per una maschera tutta a
zero (tutte le caselle deselezionate): la maschera è una **whitelist** di tipi
permessi, esattamente come leggono le caselle web.

Entrambe le direzioni usano lo stesso classificatore e gli stessi bit
(``g_config.rf2inetFilter`` per RF→INET, ``g_config.inet2rfFilter`` per INET→RF),
così che le due non possano mai divergere.

Gate di portata locale (RF→INET)
================================

Quando ``rf2inet_range_en`` è attivo e ``rf2inet_range_km`` > 0, un pacchetto la
cui posizione decodifica a più di quei chilometri da "My Station"
(``my_lat``/``my_lon``) viene scartato. La distanza è il cerchio massimo
(``aprs_filter_haversine_km()``) tra i due punti. I pacchetti la cui posizione
non può essere decodificata passano questo controllo.
``aprs_filter_decode_position()`` supporta sia layout non compressi
(``DDMM.hhN/DDDMM.hhW``) sia compressi base-91 per i DTI che portano una posizione
nel campo info (``!``/``=``, ``/``/``@``, ``;`` oggetto, ``)`` item). I report
Mic-E portano la posizione nel campo di destinazione AX.25 e non sono
decodificabili dal solo campo info.

Gate di prefisso locale (RF→INET)
=================================

Quando ``rf2inet_prefix_en`` è attivo, l'indicativo di origine deve iniziare con
uno dei prefissi separati da virgole in ``rf2inet_prefixes`` (es. ``EA,EB,EC``).
Insensibile a maiuscole/minuscole; lo spazio bianco intorno alle voci è ignorato.
Un indicativo corrisponde se inizia con qualsiasi prefisso elencato
(``aprs_filter_prefix_match()``).

Whitelist / blacklist di indicativi (budlist)
=============================================

Una lista di indicativi condivisa (``g_config.budlist[]``, indicativo base, senza
SSID) con una **modalità per direzione**:

* ``BUDLIST_OFF`` — il filtro di indicativi è disabilitato per questa direzione.
* ``BUDLIST_WHITELIST`` — solo gli indicativi nella lista possono passare.
* ``BUDLIST_BLACKLIST`` — gli indicativi nella lista sono bloccati; il resto
  passa.

``aprs_filter_budlist_pass()`` confronta senza distinguere maiuscole e rimuove
internamente qualsiasi suffisso ``-SSID``, così che sia i chiamanti RF (solo
indicativo base) sia INET (possono portare ``-SSID``) passino il loro indicativo
direttamente.

Unwrap selettivo di terze parti (solo INET→RF)
==============================================

I payload di terze parti (``}``) classificano come ``0`` e non passano mai di
default — reinoltrarli senza restrizioni è la causa numero uno di loop IGate.
Quando ``inet2rf_3rdparty_unwrap_en`` è attivo **e** ``inet2rf_budlist_mode ==
BUDLIST_WHITELIST``, ``aprs_filter_classify_thirdparty_inner()`` valuta il
payload *dentro* un livello di incapsulamento ``}`` così che il chiamante possa
ritrasmetterlo — ma solo dopo aver verificato che l'origine del pacchetto interno
sia essa stessa nella whitelist. Non è mai un interruttore generale di
"ritrasmetti tutto il traffico di terze parti".

Validazione del filtro server
=============================

``aprs_filter_validate_server_string()`` controlla la *grammatica* di
``g_config.aprs_filter`` prima dell'invio: ogni termine separato da spazi deve
essere ``<lettera>/<argomenti>`` con una lettera di filtro nota e la forma di
argomenti corretta per quella lettera (``r`` necessita esattamente 3 argomenti
numerici, ``p`` necessita almeno un prefisso, …). Valida solo la struttura, non
se i valori di coordinata/distanza sono sensati.
