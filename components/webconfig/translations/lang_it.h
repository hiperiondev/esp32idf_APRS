/**
 * @file lang_it.h
 *
 * @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
 * @date 2026
 * @copyright GNU General Public License v3
 * @see https://github.com/hiperiondev/esp32idf_APRS
 *
 * @note
 * This is based on other projects:
 *     VP-Digi: https://github.com/sq8vps/vp-digi
 *     ESP32APRS: https://github.com/nakhonthai/ESP32APRS_Audio
 *     LibAPRS: https://github.com/markqvist/LibAPRS
 *
 *     please contact their authors for more information.
 *
 * @brief Italian strings.
 *
 * Only ever included by translations.h when LANGUAGE == LANG_IT. Keep this file's
 * TR_xxx macro list identical (same names) across all lang_xx.h files - only the
 * string contents should differ.
 */

#ifndef LANG_IT_H
#define LANG_IT_H

/**
 * @name Brand / chrome
 * @{
 */
#define TR_BRAND            "Amministrazione Web esp32idf_APRS"
#define TR_LOGOUT           "Disconnetti"
#define TR_LOGGED_OUT_TITLE "Disconnesso"
#define TR_LOG_IN_AGAIN     "Accedi di nuovo"
#define TR_UNAUTHORIZED     "401 Non autorizzato"
#define TR_FORBIDDEN_CSRF   "403 Vietato: impossibile verificare l'origine della richiesta"
#define TR_SAVED_REDIRECT   "Salvato. Reindirizzamento in corso..."

/** @} */

/**
 * @name Sidebar menu
 * @{
 */
#define TR_MENU_DASHBOARD "Pannello"
#define TR_MENU_MSGCHAT   "Inv/Ric Msg"
#define TR_MENU_BULLETINS "Bollettini"
#define TR_MENU_OBJITEMS  "Oggetti e Item"
#define TR_MENU_STATION   "Stazione"
#define TR_MENU_RADIO     "Radiomodem"
#define TR_MENU_MSG       "Messaggio"
#define TR_MENU_QUERY     "Interrogazione"
#define TR_MENU_IGATE     "IGate"
#define TR_MENU_DIGI      "Digipeater"
#define TR_MENU_TRACKER   "Tracker"
#define TR_MENU_WX        "Meteo"
#define TR_MENU_TLM       "Telemetria"
#define TR_MENU_SYSTEM    "Sistema"
#define TR_MENU_WIRELESS  "Wireless"
#define TR_MENU_STORAGE   "Archiviazione file"
#define TR_MENU_ABOUT     "Firmware"

/** @} */

/**
 * @name Common buttons / widgets
 * @{
 */
#define TR_BTN_SAVE          "Salva"
#define TR_BTN_AUTO_GENERATE "Genera automaticamente"
#define TR_BTN_LOOP_TEST     "TEST LOOP"
#define TR_LOOPTEST_SAVING   "Salvataggio impostazioni..."
#define TR_LOOPTEST_RUNNING  "Test in corso..."
#define TR_LOOPTEST_FAILED   "Richiesta non riuscita"
#define TR_SHOW_PASSWORD     "Mostra password"

/** @} */

/**
 * @name page_about.c
 * @{
 */
#define TR_ABOUT_TITLE       "Firmware"
#define TR_ABOUT_FW_LEGEND   "Firmware"
#define TR_ABOUT_PROJECT     "Progetto:"
#define TR_ABOUT_VERSION     "Versione:"
#define TR_ABOUT_BUILD_DATE  "Data di compilazione:"
#define TR_ABOUT_IDF_VERSION "Versione IDF:"
#define TR_ABOUT_PARTITION   "Partizione in esecuzione:"
#define TR_ABOUT_OTA_LEGEND  "Aggiornamento OTA"
#define TR_ABOUT_OTA_BODY                                                                                                                                      \
    "Carica un nuovo firmware .bin compilato per questa scheda. Viene scritto "                                                                                \
    "nella partizione OTA inattiva mentre il dispositivo continua a funzionare "                                                                               \
    "con quella attuale; il dispositivo passa alla nuova immagine e si riavvia "                                                                               \
    "solo al termine del caricamento e dopo la verifica. Se il nuovo firmware "                                                                                \
    "non si avvia correttamente, viene ripristinato automaticamente al riavvio successivo."
#define TR_OTA_TARGET_SLOT      "Partizione di destinazione:"
#define TR_OTA_SELECT_FILE      "File firmware (.bin):"
#define TR_OTA_UPLOAD_BTN       "Carica e flasha"
#define TR_OTA_CONFIRM          "Caricare e flashare questo firmware? Il dispositivo si riavvierà al termine."
#define TR_OTA_NO_FILE_SELECTED "Seleziona prima un file firmware .bin."
#define TR_OTA_UPLOADING        "Caricamento e scrittura sulla memoria flash..."
#define TR_OTA_NO_PARTITION                                                                                                                                    \
    "Nessuna partizione OTA disponibile nella tabella delle partizioni di questo dispositivo. Riflashalo una volta via USB/UART con l'attuale partitions.csv " \
    "per abilitare l'OTA."
#define TR_OTA_BEGIN_FAILED    "Impossibile avviare la scrittura OTA: "
#define TR_OTA_NO_FILE_CHOSEN  "nessun file ricevuto"
#define TR_OTA_UPLOAD_FAILED   "Caricamento del firmware non riuscito"
#define TR_OTA_VALIDATE_FAILED "convalida dell'immagine non riuscita: il file non è un'immagine firmware valida per questa scheda"
#define TR_OTA_SUCCESS         "Firmware scritto e verificato correttamente."
#define TR_OTA_REBOOTING       "Riavvio con il nuovo firmware in corso..."

/** @} */

/**
 * @name Common field/fieldset labels (auto-extracted from pages/<page>.c source files)
 * @{
 */
#define TR_F_ADD_TIMESTAMP                                 "Aggiungi timestamp"
#define TR_F_ALTITUDE_M                                    "Altitudine (m)"
#define TR_F_APRS_IS_SERVER                                "Server APRS-IS"
#define TR_F_APRS_MESSAGING                                "Messaggistica APRS"
#define TR_F_APRS_PASSCODE                                 "Passcode APRS-IS"
#define TR_F_APRS_SYMBOLS                                  "Simboli APRS"
#define TR_F_AUDIO_AFSK                                    "Audio / AFSK"
#define TR_F_ENABLE_AUDIO_MODEM                            "Abilita modem audio ADC/DAC"
#define TR_F_AFSK_MODULATION                               "Modulazione"
#define TR_F_AUDIO_LOW_PASS_FILTER                         "Filtro passa-basso audio"
#define TR_F_AUTO_WIDEN_N                                  "Auto (WIDEn-N)"
#define TR_F_BEACON_INTERVAL_S                             "Intervallo beacon (s)"
#define TR_F_BEACON_POSITION                               "Beacon / Posizione"
#define TR_F_BEACON_POSITION_2                             "Posizione beacon"
#define TR_F_BEACON_VIA_INTERNET                           "Beacon via Internet"
#define TR_F_BEACON_VIA_RF                                 "Beacon via RF"
#define TR_F_COMMENT                                       "Commento"
#define TR_F_COMPRESS_POSITION                             "Comprimi posizione"
#define TR_F_DASHBOARD                                     "Pannello"
#define TR_F_DATA_INTERVAL_S                               "Intervallo dati (s)"
#define TR_F_DIGIPEATER                                    "Digipeater"
#define TR_F_DIGI_DELAY_MS                                 "Ritardo digi (ms)"
#define TR_F_DUPE_FILTER_WINDOW_S                          "Finestra filtro duplicati (s)"
#define TR_F_ENABLE                                        "Abilita"
#define TR_F_ENABLE_DIGIPEATER                             "Abilita Digipeater"
#define TR_F_ENABLE_IGATE                                  "Abilita IGate"
#define TR_F_ENABLE_MESSAGING                              "Abilita messaggistica"
#define TR_F_ENABLE_TRACKER                                "Abilita Tracker"
#define TR_F_ENABLE_WX                                     "Abilita WX"
#define TR_F_FILE_STORAGE                                  "Archiviazione file"
#define TR_F_FILTER                                        "Filtro"
#define TR_F_FIXED_ALTITUDE_M                              "Altitudine fissa (m)"
#define TR_F_FIXED_INTERVAL_S                              "Intervallo fisso (s)"
#define TR_F_FIXED_LATITUDE                                "Latitudine fissa"
#define TR_F_FIXED_LONGITUDE                               "Longitudine fissa"
#define TR_F_FX_25_FORWARD_ERROR_CORRECTED_AX_25           "FX.25 (AX.25 con correzione d'errore)"
#define TR_F_IGATE                                         "IGate"
#define TR_F_INCLUDE_ALTITUDE                              "Includi altitudine"
#define TR_F_INCLUDE_RSSI                                  "Includi RSSI"
#define TR_F_INTERNET_TO_RF                                "Internet verso RF"
#define TR_F_LATITUDE                                      "Latitudine"
#define TR_F_LOG_TRACK                                     "Registra traccia"
#define TR_F_LONGITUDE                                     "Longitudine"
#define TR_F_MESSAGE                                       "Messaggio"
#define TR_F_MESSAGE_ALARM_ENABLE                          "Abilita allarme messaggi"
#define TR_F_MESSAGE_ALARM_PIN                             "Pin allarme messaggi"
#define TR_F_MICE_POSITION                                 "Codifica posizione Mic-E"
#define TR_F_MODE                                          "Modalità"
#define TR_F_MY_CALLSIGN                                   "Mio nominativo"
#define TR_USE_MY_STATION_DATA                             "Usa i dati della mia stazione"
#define TR_F_NAME                                          "Nome"
#define TR_F_OBJECT_ITEM_NAME                              "Nome oggetto/item"
#define TR_F_OBJECT_NAME                                   "Nome oggetto"
#define TR_F_OPTIONS                                       "Opzioni"
#define TR_F_PARM_UNIT_EQNS_INTERVAL_S                     "Intervallo PARM/UNIT/EQNS (s)"
#define TR_F_PASSWORD                                      "Password"
#define TR_F_PHG                                           "PHG"
#define TR_F_POSITION                                      "Posizione"
#define TR_F_PREAMBLE_MS                                   "Preambolo (ms)"
#define TR_DISABLED                                        "Disabilitato"
#define TR_GPIO_USED_BY                                    "GPIO%d (utilizzato: %.30s)"
#define TR_F_PROTOCOL                                      "Protocollo"
#define TR_F_QUERY                                         "Interrogazione"
#define TR_F_ENABLE_QUERY                                  "Abilita risponditore interrogazioni"
#define TR_F_QUERY_RF                                       "RF"
#define TR_F_QUERY_INET                                     "APRS-IS"
#define TR_F_QUERY_APRS                                     "?APRS? - interrogazione generale stazione"
#define TR_F_QUERY_WX                                       "?WX? - richiesta rapporto meteo"
#define TR_F_QUERY_IGATE                                    "?IGATE? - richiesta stato IGate"
#define TR_F_QUERY_DIRECTED                                 "Interrogazioni dirette (CALL:?query?)"
#define TR_F_QUERY_MIN_INTERVAL                             "Secondi minimi tra risposte identiche"
#define TR_F_RADIO_MODEM                                   "Radiomodem"
#define TR_F_RETRY_COUNT                                   "Numero tentativi"
#define TR_F_RETRY_INTERVAL_S                              "Intervallo tentativi (s)"
#define TR_F_RF_TO_INTERNET                                "RF verso Internet"
#define TR_F_RF_TX_BUFFERS                                 "Buffer TX"
#define TR_F_PTT_MIN_UNKEY_MS                              "Tempo minimo PTT sbloccato (ms)"
#define TR_F_CSMA_PERSISTENCE                              "Persistenza CSMA (p, 1-255)"
#define TR_F_SEND_RECEIVE_VIA_INTERNET                     "Invia/ricevi via Internet"
#define TR_F_SEND_RECEIVE_VIA_RF                           "Invia/ricevi via RF"
#define TR_F_SEND_VIA_INTERNET                             "Invia via Internet"
#define TR_F_SEND_VIA_RF                                   "Invia via RF"
#define TR_F_SENSOR_MAPPING_ENABLE_AVERAGED_SOURCE_CHANNEL "Mappatura sensori (abilita / media / canale sorgente)"
#define TR_F_SERVER_HOST                                   "Host server"
#define TR_F_SERVER_PORT                                   "Porta server"
#define TR_F_SSID                                          "SSID"
#define TR_F_STATION                                       "Stazione"
#define TR_F_BULLETINS                                     "Bollettini"
#define TR_F_BULLETIN_FMT                                  "Bollettino %d"
#define TR_F_BULLETIN_MSG                                  "Messaggio (max 67 caratteri)"
#define TR_F_BULLETIN_EXPIRE                               "Scadenza (ore, 0 = mai)"
#define TR_F_OBJITEMS                                      "Oggetti e Item"
#define TR_F_OBJITEM_FMT                                   "Oggetto/Item %d"
#define TR_F_OBJITEM_TX_CONTROL                            "Controllo Trasmissione"
#define TR_F_OBJITEM_IDENTITY                              "Identità e Stato"
#define TR_F_OBJITEM_POS_SYMBOL                            "Posizione e Simbolo"
#define TR_F_OBJITEM_AREA_SECTION                          "Oggetto Area"
#define TR_F_OBJITEM_SIGNPOST_SECTION                      "Cartello (Signpost)"
#define TR_F_OBJITEM_REPEATER_SECTION                      "Parametri Radio del Ripetitore"
#define TR_F_OBJITEM_TIMING_SECTION                        "Temporizzazione Beacon"
#define TR_F_OBJITEM_TYPE                                  "Tipo"
#define TR_F_OBJITEM_TYPE_OBJECT                           "Oggetto (con timestamp)"
#define TR_F_OBJITEM_TYPE_ITEM                             "Item (permanente)"
#define TR_F_OBJITEM_ACTIVE                                "Attivo (deseleziona = elimina)"
#define TR_F_OBJITEM_SCOPE                                 "Ambito"
#define TR_F_OBJITEM_SCOPE_PRIVATE                         "Privato (non trasmesso)"
#define TR_F_OBJITEM_SCOPE_LOCAL                           "Locale (solo RF)"
#define TR_F_OBJITEM_SCOPE_GLOBAL                          "Globale (RF + Internet)"
#define TR_F_OBJITEM_SYMBOL                                "Simbolo / overlay"
#define TR_F_OBJITEM_COURSE                                "Rotta (gradi, 0-359)"
#define TR_F_OBJITEM_SPEED                                 "Velocità (nodi, 0 = ometti)"
#define TR_F_OBJITEM_AREA_SHAPE                            "Forma area (simbolo \\l)"
#define TR_F_OBJITEM_SHAPE_CIRCLE                          "Cerchio"
#define TR_F_OBJITEM_SHAPE_LINE                            "Linea"
#define TR_F_OBJITEM_SHAPE_ELLIPSE                         "Ellisse"
#define TR_F_OBJITEM_SHAPE_TRIANGLE                        "Triangolo"
#define TR_F_OBJITEM_SHAPE_BOX                             "Rettangolo"
#define TR_F_OBJITEM_SHAPE_FILLED                          " (pieno)"
#define TR_F_OBJITEM_AREA_COLOR                            "Colore area (0-15)"
#define TR_F_OBJITEM_AREA_LAT_OFF                          "Offset latitudine area (gradi)"
#define TR_F_OBJITEM_AREA_LON_OFF                          "Offset longitudine area (gradi)"
#define TR_F_OBJITEM_SIGNPOST                              "Testo segnale (simbolo \\m, 3 caratteri)"
#define TR_F_OBJITEM_FREQ                                  "Frequenza di monitoraggio (MHz, 0 = nessuna)"
#define TR_F_OBJITEM_DUPLEX                                "Direzione duplex"
#define TR_F_OBJITEM_DUPLEX_SIMPLEX                        "Simplex"
#define TR_F_OBJITEM_DUPLEX_PLUS                           "Positivo (+)"
#define TR_F_OBJITEM_DUPLEX_MINUS                          "Negativo (-)"
#define TR_F_OBJITEM_OFFSET                                "Offset duplex (kHz)"
#define TR_F_OBJITEM_TONE                                  "Tono subaudio CTCSS (Hz, 0 = nessuno)"
#define TR_F_OBJITEM_PATH_FMT                              "Percorso %d"
#define TR_F_OBJITEM_QRU                                   "Appartenenza gruppo QRU"
#define TR_F_OBJITEM_QRU_NONE                              "(nessuno)"
#define TR_F_QRU_AMBU                                      "ambulanza"
#define TR_F_QRU_CLUB                                      "club di radioamatori"
#define TR_F_QRU_ECHO                                      "Echolink"
#define TR_F_QRU_FIRE                                      "caserma dei pompieri"
#define TR_F_QRU_FOOD                                      "ristoranti"
#define TR_F_QRU_FUEL                                      "stazioni di servizio/benzinai"
#define TR_F_QRU_HOSP                                      "ospedali"
#define TR_F_QRU_LIFEBOAT                                  "scialuppe di salvataggio"
#define TR_F_QRU_LTHS                                      "fari"
#define TR_F_QRU_POLI                                      "stazioni di polizia"
#define TR_F_QRU_POST                                      "uffici postali"
#define TR_F_QRU_RD13                                      "ripetitori D-Star 13cm"
#define TR_F_QRU_RD23                                      "ripetitori D-Star 23cm"
#define TR_F_QRU_RD2M                                      "ripetitori D-Star 2m"
#define TR_F_QRU_RD3C                                      "ripetitori D-Star 3cm"
#define TR_F_QRU_RD70                                      "ripetitori D-Star 70cm"
#define TR_F_QRU_RP10                                      "ripetitori analogici 10m"
#define TR_F_QRU_RP13                                      "ripetitori analogici 13cm"
#define TR_F_QRU_RP23                                      "ripetitori analogici 23cm"
#define TR_F_QRU_RP2M                                      "ripetitori analogici 2m"
#define TR_F_QRU_RP3C                                      "ripetitori analogici 3cm"
#define TR_F_QRU_RP6M                                      "ripetitori analogici 6m"
#define TR_F_QRU_RP70                                      "ripetitori analogici 70cm"
#define TR_F_QRU_RT13                                      "ripetitori televisivi 13cm"
#define TR_F_QRU_RT23                                      "ripetitori televisivi 23cm"
#define TR_F_QRU_RT3C                                      "ripetitori televisivi 3cm"
#define TR_F_QRU_SRAIL                                     "ferrovia a vapore"
#define TR_F_QRU_STOR                                      "negozi di radioamatori"
#define TR_F_QRU_T2SRV                                     "posizioni approx. dei server APRS-IS Tier 2"
#define TR_F_QRU_VETE                                      "veterinari"
#define TR_F_QRU_WOTA                                      "Wainwrights On The Air"
#define TR_F_OBJITEM_INIT_RATE                             "Intervallo iniziale (s)"
#define TR_F_OBJITEM_SLOW_RATE                             "Intervallo lento (s, 0 = nessun decadimento)"
#define TR_F_OBJITEM_DECAY                                 "Rapporto di decadimento (es. 2.0, <1 = nessuno)"
#define TR_NOTE_OBJITEM                                                                                                                                        \
    "Gli Oggetti hanno un timestamp (;NOME); gli Item sono permanenti ()NOME). Deselezionando Attivo si inviano report di eliminazione, poi si disabilita "    \
    "automaticamente. L'Ambito limita la trasmissione indipendentemente dalle caselle RF/Internet."
#define TR_F_STATUS_BEACON           "Beacon di stato"
#define TR_F_STATUS_INTERVAL_S_0_OFF "Intervallo stato (s, 0=off)"
#define TR_F_STATUS_TEXT             "Testo di stato"
#define TR_F_SYMBOL_IDLE             "Simbolo (inattivo)"
#define TR_F_SYMBOL_MOVING           "Simbolo (in movimento)"
#define TR_F_SYMBOL_STOPPED          "Simbolo (fermo)"
#define TR_F_SYSTEM                  "Sistema"
#define TR_F_TELEMETRY               "Telemetria"
#define TR_F_BEACON                  "Beacon"
#define TR_F_TRACKER                 "Tracker"
#define TR_F_TX_TIME_SLOT_MS         "Slot temporale TX (ms)"
#define TR_F_UPLOAD                  "Carica"
#define TR_F_USERNAME                "Nome utente"
#define TR_F_WEATHER                 "Meteo"
#define TR_F_WEATHER_STATION         "Stazione meteo"
#define TR_F_WIRELESS                "Wireless"

#define TR_F_OFF "Off"

/** @} */

/**
 * @name page_common.c: dashboard / sysinfo
 * @{
 */
#define TR_ENABLED               "abilitato"
#define TR_DASH_DIGI_SHORT       "Digi"
#define TR_DASH_WX_SHORT         "WX:"
#define TR_DASH_UPTIME           "Uptime:"
#define TR_DASH_FREE_HEAP        "Heap libero:"
#define TR_DASH_LITTLEFS         "LittleFS:"
#define TR_DASH_SYSINFO          "Informazioni sistema"
#define TR_DASH_IGATE_TRAFFIC    "Traffico IGate"
#define TR_TRAFFIC_PAUSE         "Pausa"
#define TR_TRAFFIC_RESUME        "Riprendi"
#define TR_TRAFFIC_CLEAR         "Cancella"
#define TR_TRAFFIC_WAITING       "In attesa di traffico..."
#define TR_TRAFFIC_COL_TIME      "ORA"
#define TR_TRAFFIC_COL_TYPE      "TIPO"
#define TR_TRAFFIC_COL_DX        "DX"
#define TR_TRAFFIC_COL_PACKET    "PACCHETTO"
#define TR_TRAFFIC_COL_AUDIO     "AUDIO"
#define TR_SYSINFO_CHIP          "Chip"
#define TR_SYSINFO_MODEL         "Modello:"
#define TR_SYSINFO_CORES         "Core:"
#define TR_SYSINFO_REVISION      "Revisione:"
#define TR_SYSINFO_CPU_FREQ      "Frequenza CPU:"
#define TR_SYSINFO_CPU_FREQ_SET  "Imposta frequenza CPU"
#define TR_SYSINFO_CPU_FREQ_NOTE "Salvato in flash e riapplicato automaticamente ad ogni avvio."
#define TR_SYSINFO_FLASH_SIZE    "Dimensione flash:"
#define TR_SYSINFO_MIN_FREE_HEAP "Heap libero minimo:"
#define TR_DASH_REBOOT_REASON    "Motivo riavvio:"

/** @} */

/**
 * @name page_common.c
 * @{
 */
#define TR_DASH_RADIO_INFO     "Info Radio"
#define TR_DASH_MODEM          "MODEM"
#define TR_DASH_FX25           "FX.25"
#define TR_DASH_APRS_IS_SERVER "SERVER APRS-IS"
#define TR_DASH_HOST           "HOST"
#define TR_DASH_PORT           "PORTA"
#define TR_DASH_WIFI           "WiFi"
#define TR_DASH_MODE           "MODALITÀ"
#define TR_DASH_SSID           "SSID"
#define TR_DASH_RSSI           "RSSI"
#define TR_DASH_DISCONNECTED   "Disconnetti"
#define TR_DASH_MODES_ENABLED  "Modalità abilitate"
#define TR_DASH_NETWORK_STATUS "Stato rete"
#define TR_DASH_STATISTICS     "STATISTICHE"
#define TR_DASH_RADIO_RX       "RADIO RX:"
#define TR_DASH_PACKET_TX      "RADIO TX:"
#define TR_DASH_RF2INET        "RF2INET:"
#define TR_DASH_INET2RF        "INET2RF:"
#define TR_DASH_IGATE_RX       "IGATE RX:"
#define TR_DASH_IGATE_TX       "IGATE TX:"
#define TR_DASH_DIGI_STAT      "DIGI:"
#define TR_DASH_DROP_ERR       "SCARTI/ERR:"
#define TR_DASH_DROP_BREAKDOWN "Dettaglio Scarti"
#define TR_DASH_TX_QUEUE       "CODA TX RF:"
#define TR_DASH_LH_ICON        "ICONA"

/** @} */

/**
 * @name page_digi.c / page_igate.c / page_tracker.c telemetry notes
 * @{
 */
#define TR_NOTE_TLM_DIGI                                                                                                                                       \
    "La telemetria (EQNS/PARM/UNIT) per i beacon Digi si configura nella pagina "                                                                              \
    "<a href='/tlm'>Telemetria</a>."
#define TR_NOTE_TLM_IGATE                                                                                                                                      \
    "La telemetria (EQNS/PARM/UNIT) per i beacon IGate si configura nella pagina "                                                                             \
    "<a href='/tlm'>Telemetria</a>."
#define TR_NOTE_TLM_TRACKER                                                                                                                                    \
    "La telemetria (EQNS/PARM/UNIT) per i beacon Tracker si configura nella pagina "                                                                           \
    "<a href='/tlm'>Telemetria</a>."

/** @} */

/**
 * @name page_mod.c
 * @{
 */
/** @} */

/**
 * @name page_msgchat.c ("Snd/Rcv Msg")
 * @{
 */
#define TR_F_SND_RCV_MSG      "Inv/Ric Msg"
#define TR_MSGCHAT_MY_STATION "La Mia Stazione:"
#define TR_MSGCHAT_DISABLED_NOTE                                                                                                                               \
    "La Messaggistica APRS è disabilitata o non è configurato alcun nominativo. Abilitala e imposta un nominativo nella pagina Message."
#define TR_MSGCHAT_LOADING          "Caricamento messaggi..."
#define TR_MSGCHAT_EMPTY            "Ancora nessun messaggio."
#define TR_MSGCHAT_TO               "A (nominativo):"
#define TR_MSGCHAT_TO_PLACEHOLDER   "N0CALL-9"
#define TR_MSGCHAT_TEXT             "Messaggio:"
#define TR_MSGCHAT_TEXT_PLACEHOLDER "Scrivi un messaggio..."
#define TR_MSGCHAT_SEND             "Invia"
#define TR_MSGCHAT_YOU              "Tu"
#define TR_MSGCHAT_ERR_EMPTY        "Inserisci un nominativo di destinazione e un messaggio."
#define TR_MSGCHAT_ERR_DISABLED     "La Messaggistica APRS è disabilitata nella pagina Message."
#define TR_MSGCHAT_ERR_NO_MYCALL    "Nessun nominativo di stazione configurato."
#define TR_MSGCHAT_SENT_OK          "Inviato."
#define TR_MSGCHAT_SENT_FAIL        "Invio non riuscito."

/** @} */

/**
 * @name page_mod.c
 * @{
 */

/** @} */

/**
 * @name page_storage.c
 * @{
 */
#define TR_STORAGE_USAGE                 "Utilizzo LittleFS:"
#define TR_STORAGE_UPLOAD_FILE           "Carica file"
#define TR_STORAGE_CONFIRM_FORMAT        "Cancellare TUTTI i file e ripristinare la configurazione predefinita?"
#define TR_STORAGE_FORMAT_BTN            "Formatta LittleFS"
#define TR_STORAGE_SIZE_BYTES            "Dimensione (byte)"
#define TR_STORAGE_ACTIONS               "Azioni"
#define TR_STORAGE_DOWNLOAD              "Scarica"
#define TR_STORAGE_CONFIRM_DELETE_PREFIX "Elimina "
#define TR_STORAGE_DELETE                "Elimina"
#define TR_STORAGE_UPLOAD_OK             "Caricato:"
#define TR_STORAGE_UPLOAD_FAILED         "Caricamento non riuscito. Controlla il file e lo spazio libero disponibile."
#define TR_STORAGE_NO_FILE_CHOSEN        "Seleziona prima un file."
#define TR_STORAGE_BACK                  "Indietro"

/** @} */

/**
 * @name page_symbol.c
 * @{
 */
#define TR_SYM_HOUSE_HF        "Casa (HF)"
#define TR_SYM_CAR             "Auto"
#define TR_SYM_MOTORCYCLE      "Moto"
#define TR_SYM_BICYCLE         "Bicicletta"
#define TR_SYM_TRUCK           "Camion"
#define TR_SYM_VAN             "Furgone"
#define TR_SYM_JEEP            "Jeep"
#define TR_SYM_FIRE_TRUCK      "Autopompa"
#define TR_SYM_POLICE          "Polizia"
#define TR_SYM_HOUSE           "Casa"
#define TR_SYM_DIGIPEATER      "Digipeater"
#define TR_SYM_GATEWAY         "Gateway"
#define TR_SYM_WEATHER_STATION "Stazione meteo"
#define TR_SYM_BALLOON         "Pallone aerostatico"
#define TR_SYM_SPACE_SHUTTLE   "Navetta spaziale"
#define TR_SYM_SAILBOAT        "Barca a vela"
#define TR_SYM_NWS_SITE        "Sito NWS"
#define TR_SYM_TCP_IP          "TCP/IP"
#define TR_SYM_CAR_ALT         "Auto (tabella alternativa)"
#define TR_SYM_WX_STATION_ALT  "Stazione WX (tabella alternativa)"
#define TR_SYM_INTRO                                                                                                                                           \
    "Riferimento rapido per i codici simbolo APRS più comuni. Ogni pagina di servizio "                                                                        \
    "(IGate / Digi / Tracker) ha il proprio campo simbolo a testo libero \u2014 copia il "                                                                     \
    "codice a 2 caratteri da qui in quel campo."
#define TR_SYM_CODE                 "Codice"
#define TR_SYM_MEANING              "Significato"
#define TR_SYM_CURRENTLY_CONFIGURED "Simboli attualmente configurati"

/** @} */

/**
 * @name page_system.c
 * @{
 */
#define TR_SYS_WEB_ADMIN_LOGIN       "Accesso amministrazione web"
#define TR_SYS_DEVICE                "Dispositivo"
#define TR_SYS_HOST_NAME             "Nome host"
#define TR_SYS_TIME_ZONE             "Fuso orario (offset UTC)"
#define TR_SYS_SYNC_NTP              "Sincronizza ora via NTP"
#define TR_SYS_NTP_HOST              "Host NTP (primario)"
#define TR_SYS_NTP_HOST2             "Host NTP (riserva 2)"
#define TR_SYS_NTP_HOST3             "Host NTP (riserva 3)"
#define TR_SYS_NTP_RESYNC            "Intervallo risincronizzazione NTP (s, min 30)"
#define TR_SYS_AUTO_RESET_TIMEOUT    "Timeout auto-reset (min, 0=off)"
#define TR_SYS_DIGI_PATH_ALIASES     "Alias percorso Digipeater"
#define TR_SYS_PATH_1                "Percorso 1"
#define TR_SYS_PATH_2                "Percorso 2"
#define TR_SYS_PATH_3                "Percorso 3"
#define TR_SYS_PATH_4                "Percorso 4"
#define TR_SYS_CONFIRM_FACTORY_RESET "Ripristinare TUTTE le impostazioni ai valori di fabbrica?"
#define TR_SYS_FACTORY_RESET         "Ripristino di fabbrica"

/** @} */

/**
 * @name page_tlm.c
 * @{
 */
#define TR_TLM_AVG "Media"
#define TR_TLM_BIT "Bit"
/** @} */

/**
 * @name Configuratore telemetria (nuova pagina)
 * @{
 */
#define TR_TLM_ENABLE_TELEMETRY   "Abilita telemetria"
#define TR_TLM_REPORT_PARAMS      "Parametri del report"
#define TR_TLM_PATH_DIGIS         "Percorso (digipeater)"
#define TR_TLM_DESTINATION        "Destinazione"
#define TR_TLM_AUTO_INC_SEQ       "Incremento automatico sequenza"
#define TR_TLM_ANALOG_FIELD_WIDTH "Larghezza campo analogico"
#define TR_TLM_FIELDW_3DIGIT      "3 cifre con zeri (000-255, rigoroso)"
#define TR_TLM_FIELDW_AUTO        "Minimo / secondo necessità (interi o decimali)"
#define TR_TLM_OMIT_TRAILING      "Ometti canali finali inutilizzati"
#define TR_TLM_TRAIL_COMMENT      "Commento finale (opzionale, dopo i bit)"
#define TR_TLM_ANALOG_COUNT       "Canali analogici inviati"
#define TR_TLM_DIGITAL_COUNT      "Bit digitali inviati"
#define TR_TLM_DEF_MESSAGES       "Messaggi di definizione"
#define TR_TLM_GEN_PARM           "PARM - nomi canale e bit"
#define TR_TLM_GEN_UNIT           "UNIT - unità / etichette stato bit"
#define TR_TLM_GEN_EQNS           "EQNS - coefficienti di scala (A,B,C)"
#define TR_TLM_GEN_BITS           "BITS - senso del bit + nome"
#define TR_TLM_ANALOG_LEGEND      "Canali analogici (A1-A5)"
#define TR_TLM_DIGITAL_LEGEND     "Canali digitali (B1-B8)"
#define TR_TLM_UNIT               "Unità"
#define TR_TLM_SOURCE             "Sorgente"
#define TR_TLM_RF                 "RF"
#define TR_TLM_RAW_MIN            "Grezzo min"
#define TR_TLM_RAW_MAX            "Grezzo max"
#define TR_TLM_COEF_A             "A (quadratico)"
#define TR_TLM_COEF_B             "B (lineare / pendenza)"
#define TR_TLM_COEF_C             "C (offset)"
#define TR_TLM_DECIMALS           "Decimali mostrati"
#define TR_TLM_ON_STATE           "Significato stato attivo"
#define TR_TLM_SENSE              "Senso"
#define TR_TLM_LABEL              "Etichetta"
#define TR_TLM_CALIB_WIZARD       "Procedura di calibrazione a 2 punti"
#define TR_TLM_CALIB_PROMPT_X1    "Lettura grezza #1 (x1):"
#define TR_TLM_CALIB_PROMPT_Y1    "Valore reale noto a x1:"
#define TR_TLM_CALIB_PROMPT_X2    "Lettura grezza #2 (x2):"
#define TR_TLM_CALIB_PROMPT_Y2    "Valore reale noto a x2:"
#define TR_TLM_CALIB_SAME_X       "x1 e x2 devono essere diversi."
#define TR_TLM_CALIB_CANCELLED    "Calibrazione annullata: inserire valori numerici."

/** @} */

/**
 * @name page_radio.c
 * @{
 */
#define TR_RADIO_AUDIO_HW_TITLE "Hardware audio (in fase di compilazione)"
#define TR_RADIO_AUDIO_HW_INFO                                                                                                                                 \
    "<br>DAC uscita: GPIO%d<br>ADC ingresso: GPIO%d<br>Pin PTT: %s<br>PTT attivo-alto: %s<br>Attenuazione ADC: %d<br>ADC: %d Hz<br>DAC: %d Hz"
#define TR_RADIO_AUDIO_HW_NOTE ""

/** @} */

/**
 * @name page_wireless.c
 * @{
 */
#define TR_WIFI_MODE_LEGEND      "Modalità WiFi"
#define TR_WIFI_STATION          "Stazione (STA)"
#define TR_WIFI_ACCESS_POINT     "Access Point (AP)"
#define TR_WIFI_AP_STA           "AP + STA"
#define TR_WIFI_TX_POWER         "Potenza TX (0-20 dBm)"
#define TR_WIFI_AP_SSID          "SSID AP"
#define TR_WIFI_AP_PASSWORD      "Password AP"
#define TR_WIFI_AP_CHANNEL       "Canale AP"
#define TR_WIFI_CLIENT_LEGEND    "Client WiFi #%d"
#define TR_BTN_WIFI_SCAN         "SCANSIONA WIFI"
#define TR_WIFI_SSID_PLACEHOLDER "Nome rete (digitalo, oppure usa Scansiona WiFi)"
#define TR_WIFI_STA_NEEDS_SSID                                                                                                                                 \
    "Salvato, ma questo NON si connetterà: la modalità seleziona una stazione, ma nessun blocco Client WiFi ha sia 'Abilita' selezionato sia un SSID "         \
    "compilato. Correggi e salva di nuovo."
#define TR_WIFI_SCANNING    "Scansione in corso..."
#define TR_WIFI_SCAN_FAILED "Scansione non riuscita"

/** @} */

/**
 * @name page_wx.c
 * @{
 */
#define TR_WX_WIND_SPEED     "Velocità vento"
#define TR_WX_WIND_GUST      "Raffica vento"
#define TR_WX_WIND_DIRECTION "Direzione vento"
#define TR_WX_TEMPERATURE    "Temperatura"
#define TR_WX_RAIN_1H        "Pioggia 1h"
#define TR_WX_RAIN_24H       "Pioggia 24h"
#define TR_WX_RAIN_MIDNIGHT  "Pioggia da mezzanotte"
#define TR_WX_HUMIDITY       "Umidità"
#define TR_WX_PRESSURE       "Pressione"
#define TR_WX_LUMINOSITY     "Luminosità"
#define TR_WX_SNOW           "Neve"
#define TR_WX_FLOOD_FT       "Livello piena (ft)"
#define TR_WX_FLOOD_M        "Livello piena"
#define TR_WX_FIELD          "Campo WX"
#define TR_WX_CHANNEL        "Canale"
#define TR_WX_CHANNEL_NONE   "(nessuno)"
#define TR_WX_VALUE          "Valore"

/** @} */

/**
 * @name IGATE page additions (station symbol, path preset, timestamp, PHG, filters)
 * @{
 */
#define TR_F_STATION_SYMBOL    "Simbolo stazione"
#define TR_F_SYMBOL_TABLE      "Tabella"
#define TR_F_SYMBOL_CODE       "Simbolo"
#define TR_BTN_PICK_SYMBOL     "..."
#define TR_SYM_PICK_HINT       "Clicca sull'icona per selezionare il simbolo"
#define TR_F_PATH              "PERCORSO"
#define TR_PATH_DIRECT         "Diretto (nessun percorso)"
#define TR_PATH_CUSTOM_UNSET   "(non impostato)"
#define TR_PATH_DIRECT_HINT    "nessun percorso digipeater - solo le stazioni che ti ricevono direttamente lo riceveranno"
#define TR_PATH_HOP_HINT       "hop via digipeater, codificato come suffisso SSID (forma breve WIDEn-N)"
#define TR_PATH_CUSTOM_HINT    "percorso digipeater personalizzato configurato nella pagina Sistema"
#define TR_F_TIME_STAMP        "Timestamp"
#define TR_F_TX_CHANNEL        "Canale TX"
#define TR_F_PHG_SECTION       "PHG"
#define TR_F_ENABLE_PHG        "Abilita PHG"
#define TR_F_RADIO_TX_POWER    "Potenza TX radio"
#define TR_F_ANTENNA_GAIN      "Guadagno antenna"
#define TR_F_HEIGHT_M          "Altezza (m)"
#define TR_F_ANTENNA_DIRECTION "Antenna/Direzione"
#define TR_F_PHG_TEXT          "Testo PHG"
#define TR_DIR_OMNI            "Omni"
#define TR_DIR_N               "N"
#define TR_DIR_NE              "NE"
#define TR_DIR_E               "E"
#define TR_DIR_SE              "SE"
#define TR_DIR_S               "S"
#define TR_DIR_SW              "SO"
#define TR_DIR_W               "O"
#define TR_DIR_NW              "NO"
#define TR_F_IGATE_FILTER      "Filtro IGate"
#define TR_F_FILTER_RF2INET    "Filtro RF verso Internet"
#define TR_F_FILTER_INET2RF    "Filtro Internet verso RF"
#define TR_FILT_MESSAGE        "Messaggio"
#define TR_FILT_STATUS         "Stato"
#define TR_FILT_TELEMETRY      "Telemetria"
#define TR_FILT_WEATHER        "Meteo"
#define TR_FILT_OBJECT         "Oggetto"
#define TR_FILT_ITEM           "Item"
#define TR_FILT_QUERY          "Interrogazione"
#define TR_FILT_BUOY           "Boa"
#define TR_FILT_POSITION       "Posizione"

#define TR_F_CALLSIGN_FILTER      "Filtro Nominativi"
#define TR_F_BUDLIST_MODE_RF2INET "Modalità RF verso Internet"
#define TR_F_BUDLIST_MODE_INET2RF "Modalità Internet verso RF"
#define TR_BUDLIST_OFF            "Disattivato"
#define TR_BUDLIST_WHITELIST      "Lista Bianca"
#define TR_BUDLIST_BLACKLIST      "Lista Nera"
#define TR_F_BUDLIST_CALL         "Nominativo"
#define TR_NOTE_BUDLIST                                                                                                                                        \
    "Elenco nominativi condiviso, fino a 8 voci. Lista Bianca: passano solo i nominativi elencati. Lista Nera: i nominativi elencati vengono bloccati."

#define TR_F_RANGE_FILTER_EN  "Abilita filtro di distanza"
#define TR_F_RANGE_KM         "Distanza massima (km, 0 = illimitata)"
#define TR_F_PREFIX_FILTER_EN "Abilita filtro prefisso nominativo"
#define TR_F_PREFIXES         "Prefissi consentiti (separati da virgola)"
#define TR_NOTE_RANGE_PREFIX                                                                                                                                   \
    "Filtro locale applicato solo a RF -> Internet, indipendente dal filtro per tipo di payload sopra. La distanza è misurata dalla posizione della Mia "      \
    "Stazione; i pacchetti la cui posizione non può essere decodificata non sono influenzati dal filtro di distanza."

#define TR_F_3RDPARTY_UNWRAP_EN "Inoltra traffico di terze parti (}) in lista bianca"
#define TR_NOTE_3RDPARTY_UNWRAP                                                                                                                                \
    "Disattivato per impostazione predefinita. Ha effetto solo quando il Filtro Nominativi Internet verso RF sopra è impostato su Lista Bianca: un pacchetto " \
    "incapsulato di terze parti viene decapsulato e inoltrato solo se il nominativo sorgente interno è a sua volta in lista bianca. Abilitare solo se ci si "  \
    "fida della fonte specifica e questa è stata inserita in lista bianca - ri-filtrare il traffico di terze parti senza questa restrizione è la causa più "   \
    "comune dei loop IGate."

#define TR_SYM_ICON            "Icona"
#define TR_SYM_QUICK_PICK      "Selezione rapida"
#define TR_SYM_PRIMARY_TABLE   "Tabella primaria ( / )"
#define TR_SYM_ALTERNATE_TABLE "Tabella alternativa ( \\ )"
#define TR_SYM_TRACKER_IDLE    "Tracker (inattivo):"
#define TR_SYM_TRACKER_MOVE    "Tracker (movimento):"
#define TR_SYM_TRACKER_STOP    "Tracker (fermo):"

/** @} */

#endif // LANG_IT_H
