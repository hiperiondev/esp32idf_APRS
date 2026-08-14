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
/** Italian text for the product name shown in the page header and browser title bar. English: "esp32idf_APRS Web Admin". */
#define TR_BRAND "Amministrazione Web esp32idf_APRS"
/** Italian text for the caption of the log-out link in the page header. English: "Logout". */
#define TR_LOGOUT "Disconnetti"
/** Italian text for the heading of the page shown after a successful log-out. English: "Logged out". */
#define TR_LOGGED_OUT_TITLE "Disconnesso"
/** Italian text for the caption of the link back to the login prompt after logging out. English: "Log in again". */
#define TR_LOG_IN_AGAIN "Accedi di nuovo"
/** Italian text for the body of the HTTP 401 response sent when authentication fails. English: "401 Unauthorized". */
#define TR_UNAUTHORIZED "401 Non autorizzato"
/** Italian text for the body of the HTTP 403 response sent when a POST fails the cross-site request check. English: "403 Forbidden: request origin could not be
 * verified". */
#define TR_FORBIDDEN_CSRF "403 Vietato: impossibile verificare l'origine della richiesta"
/** Italian text for the interstitial shown after a successful save, while the browser is redirected back to the form. English: "Saved. Redirecting...". */
#define TR_SAVED_REDIRECT "Salvato. Reindirizzamento in corso..."
/** Italian text for the warning shown when settings were accepted but could not be committed to flash. English: "Save failed: the settings could not be written
 * to flash. They are still in effect until...". */
#define TR_SAVE_FAILED "Salvataggio non riuscito: non è stato possibile scrivere le impostazioni nella memoria flash. Restano attive fino al prossimo riavvio."

/** @} */

/**
 * @name Sidebar menu
 * @{
 */
/** Italian text for the sidebar navigation entry for the Dashboard page, rendered on the sidebar. English: "Dashboard". */
#define TR_MENU_DASHBOARD "Pannello"
/** Italian text for the sidebar navigation entry for the Send/Receive Message page, rendered on the sidebar. English: "Snd/Rcv Msg". */
#define TR_MENU_MSGCHAT "Inv/Ric Msg"
/** Italian text for the sidebar navigation entry for the Bulletins page, rendered on the sidebar. English: "Bulletins". */
#define TR_MENU_BULLETINS "Bollettini"
/** Italian text for the sidebar navigation entry for the Objects and Items page, rendered on the sidebar. English: "Objects and Items". */
#define TR_MENU_OBJITEMS "Oggetti e Item"
/** Italian text for the sidebar navigation entry for the Station page, rendered on the sidebar. English: "Station". */
#define TR_MENU_STATION "Stazione"
/** Italian text for the sidebar navigation entry for the Radiomodem page, rendered on the sidebar. English: "Radiomodem". */
#define TR_MENU_RADIO "Radiomodem"
/** Italian text for the sidebar navigation entry for the Message page, rendered on the sidebar. English: "Message". */
#define TR_MENU_MSG "Messaggio"
/** Italian text for the sidebar navigation entry for the Query page, rendered on the sidebar. English: "Query". */
#define TR_MENU_QUERY "Interrogazione"
/** Italian text for the sidebar navigation entry for the IGate page, rendered on the sidebar. English: "IGate". */
#define TR_MENU_IGATE "IGate"
/** Italian text for the sidebar navigation entry for the Digipeater page, rendered on the sidebar. English: "Digipeater". */
#define TR_MENU_DIGI "Digipeater"
/** Italian text for the sidebar navigation entry for the Tracker page, rendered on the sidebar. English: "Tracker". */
#define TR_MENU_TRACKER "Tracker"
/** Italian text for the sidebar navigation entry for the Weather page, rendered on the sidebar. English: "Weather". */
#define TR_MENU_WX "Meteo"
/** Italian text for the sidebar navigation entry for the Telemetry page, rendered on the sidebar. English: "Telemetry". */
#define TR_MENU_TLM "Telemetria"
/** Italian text for the sidebar navigation entry for the System page, rendered on the sidebar. English: "System". */
#define TR_MENU_SYSTEM "Sistema"
/** Italian text for the sidebar navigation entry for the Wireless page, rendered on the sidebar. English: "Wireless". */
#define TR_MENU_WIRELESS "Senza fili"
/** Italian text for the sidebar navigation entry for the File Storage page, rendered on the sidebar. English: "File Storage". */
#define TR_MENU_STORAGE "Archiviazione file"
/** Italian text for the sidebar navigation entry for the Firmware page, rendered on the sidebar. English: "Firmware". */
#define TR_MENU_ABOUT "Firmware"

/** @} */

/**
 * @name Common buttons / widgets
 * @{
 */
/** Italian text for the caption of the "save" button, rendered on any page. English: "Save". */
#define TR_BTN_SAVE "Salva"
/** Italian text for the caption of the "auto generate" button, rendered on any page. English: "Auto Generate". */
#define TR_BTN_AUTO_GENERATE "Genera automaticamente"
/** Italian text for the caption of the "loop test" button, rendered on any page. English: "LOOP TEST". */
#define TR_BTN_LOOP_TEST "TEST LOOP"
/** Italian text for the loopback-test status message: saving, rendered on any page. English: "Saving settings...". */
#define TR_LOOPTEST_SAVING "Salvataggio impostazioni..."
/** Italian text for the loopback-test status message: running, rendered on any page. English: "Testing...". */
#define TR_LOOPTEST_RUNNING "Test in corso..."
/** Italian text for the loopback-test status message: failed, rendered on any page. English: "Request failed". */
#define TR_LOOPTEST_FAILED "Richiesta non riuscita"
/** Italian text for the caption of the checkbox that reveals a masked password field. English: "Show password". */
#define TR_SHOW_PASSWORD "Mostra password"

/** @} */

/**
 * @name page_about.c
 * @{
 */
/** Italian text for the firmware page label for title, rendered on the Firmware page. English: "Firmware". */
#define TR_ABOUT_TITLE "Firmware"
/** Italian text for the firmware page label for fw legend, rendered on the Firmware page. English: "Firmware". */
#define TR_ABOUT_FW_LEGEND "Firmware"
/** Italian text for the firmware page label for project, rendered on the Firmware page. English: "Project:". */
#define TR_ABOUT_PROJECT "Progetto:"
/** Italian text for the firmware page label for version, rendered on the Firmware page. English: "Version:". */
#define TR_ABOUT_VERSION "Versione:"
/** Italian text for the firmware page label for build date, rendered on the Firmware page. English: "Build date:". */
#define TR_ABOUT_BUILD_DATE "Data di compilazione:"
/** Italian text for the firmware page label for idf version, rendered on the Firmware page. English: "IDF version:". */
#define TR_ABOUT_IDF_VERSION "Versione IDF:"
/** Italian text for the firmware page label for partition, rendered on the Firmware page. English: "Running partition:". */
#define TR_ABOUT_PARTITION "Partizione in esecuzione:"
/** Italian text for the firmware page label for ota legend, rendered on the Firmware page. English: "OTA Update". */
#define TR_ABOUT_OTA_LEGEND "Aggiornamento OTA"
/** Italian text for the firmware page label for ota body, rendered on the Firmware page. English: "Upload a new firmware .bin built for this board. It is
 * written to the inactive OTA slot...". */
#define TR_ABOUT_OTA_BODY                                                                                                                                      \
    "Carica un nuovo firmware .bin compilato per questa scheda. Viene scritto "                                                                                \
    "nella partizione OTA inattiva mentre il dispositivo continua a funzionare "                                                                               \
    "con quella attuale; il dispositivo passa alla nuova immagine e si riavvia "                                                                               \
    "solo al termine del caricamento e dopo la verifica. Se il nuovo firmware "                                                                                \
    "non si avvia correttamente, viene ripristinato automaticamente al riavvio successivo."
/** Italian text for the firmware-update label or status message for target slot, rendered on the Firmware page. English: "Target slot:". */
#define TR_OTA_TARGET_SLOT "Partizione di destinazione:"
/** Italian text for the firmware-update label or status message for select file, rendered on the Firmware page. English: "Firmware file (.bin):". */
#define TR_OTA_SELECT_FILE "File firmware (.bin):"
/** Italian text for the firmware-update label or status message for upload btn, rendered on the Firmware page. English: "Upload &amp; Flash". */
#define TR_OTA_UPLOAD_BTN "Carica e flasha"
/** Italian text for the firmware-update label or status message for confirm, rendered on the Firmware page. English: "Upload and flash this firmware? The
 * device will reboot when done.". */
#define TR_OTA_CONFIRM "Caricare e flashare questo firmware? Il dispositivo si riavvierà al termine."
/** Italian text for the firmware-update label or status message for no file selected, rendered on the Firmware page. English: "Choose a firmware .bin file
 * first.". */
#define TR_OTA_NO_FILE_SELECTED "Seleziona prima un file firmware .bin."
/** Italian text for the firmware-update label or status message for uploading, rendered on the Firmware page. English: "Uploading and writing to flash...". */
#define TR_OTA_UPLOADING "Caricamento e scrittura sulla memoria flash..."
/** Italian text for the firmware-update label or status message for no partition, rendered on the Firmware page. English: "No OTA update slot is available on
 * this device's partition table. Reflash it once over...". */
#define TR_OTA_NO_PARTITION                                                                                                                                    \
    "Nessuna partizione OTA disponibile nella tabella delle partizioni di questo dispositivo. Riflashalo una volta via USB/UART con l'attuale partitions.csv " \
    "per abilitare l'OTA."
/** Italian text for the firmware-update label or status message for begin failed, rendered on the Firmware page. English: "Could not start the OTA write:". */
#define TR_OTA_BEGIN_FAILED "Impossibile avviare la scrittura OTA: "
/** Italian text for the firmware-update label or status message for no file chosen, rendered on the Firmware page. English: "no file was received". */
#define TR_OTA_NO_FILE_CHOSEN "nessun file ricevuto"
/** Italian text for the firmware-update label or status message for upload failed, rendered on the Firmware page. English: "Firmware upload failed". */
#define TR_OTA_UPLOAD_FAILED "Caricamento del firmware non riuscito"
/** Italian text for the firmware-update label or status message for validate failed, rendered on the Firmware page. English: "image validation failed - the
 * file is not a valid firmware image for this board". */
#define TR_OTA_VALIDATE_FAILED "convalida dell'immagine non riuscita: il file non è un'immagine firmware valida per questa scheda"
/** Italian text for the firmware-update label or status message for success, rendered on the Firmware page. English: "Firmware written and verified
 * successfully.". */
#define TR_OTA_SUCCESS "Firmware scritto e verificato correttamente."
/** Italian text for the firmware-update label or status message for rebooting, rendered on the Firmware page. English: "Rebooting into the new firmware
 * now...". */
#define TR_OTA_REBOOTING "Riavvio con il nuovo firmware in corso..."

/** @} */

/**
 * @name Common field/fieldset labels (auto-extracted from pages/<page>.c source files)
 * @{
 */
/** Italian text for the form label for the "add timestamp" field or fieldset, rendered on the configuration forms. English: "Add timestamp". */
#define TR_F_ADD_TIMESTAMP "Aggiungi timestamp"
/** Italian text for the form label for the "altitude m" field or fieldset, rendered on the configuration forms. English: "Altitude (m)". */
#define TR_F_ALTITUDE_M "Altitudine (m)"
/** Italian text for the form label for the "aprs is server" field or fieldset, rendered on the configuration forms. English: "APRS-IS Server". */
#define TR_F_APRS_IS_SERVER "Server APRS-IS"
/** Italian text for the form label for the "aprs messaging" field or fieldset, rendered on the configuration forms. English: "APRS Messaging". */
#define TR_F_APRS_MESSAGING "Messaggistica APRS"
/** Italian text for the form label for the "aprs passcode" field or fieldset, rendered on the configuration forms. English: "APRS-IS Passcode". */
#define TR_F_APRS_PASSCODE "Passcode APRS-IS"
/** Italian text for the form label for the "aprs symbols" field or fieldset, rendered on the configuration forms. English: "APRS Symbols". */
#define TR_F_APRS_SYMBOLS "Simboli APRS"
/** Italian text for the form label for the "audio afsk" field or fieldset, rendered on the configuration forms. English: "Audio / AFSK". */
#define TR_F_AUDIO_AFSK "Audio / AFSK"
/** Italian text for the form label for the "enable audio modem" field or fieldset, rendered on the configuration forms. English: "Enable audio ADC/DAC modem".
 */
#define TR_F_ENABLE_AUDIO_MODEM "Abilita modem audio ADC/DAC"
/** Italian text for the form label for the "afsk modulation" field or fieldset, rendered on the configuration forms. English: "Modulation". */
#define TR_F_AFSK_MODULATION "Modulazione"
/** Italian text for the form label for the "audio low pass filter" field or fieldset, rendered on the configuration forms. English: "Audio low-pass filter". */
#define TR_F_AUDIO_LOW_PASS_FILTER "Filtro passa-basso audio"
/** Italian text for the form label for the "beacon interval s" field or fieldset, rendered on the configuration forms. English: "Beacon interval (s)". */
#define TR_F_BEACON_INTERVAL_S "Intervallo beacon (s)"
/** Italian text for the form label for the "beacon position" field or fieldset, rendered on the configuration forms. English: "Beacon / Position". */
#define TR_F_BEACON_POSITION "Beacon / Posizione"
/** Italian text for the form label for the "beacon position 2" field or fieldset, rendered on the configuration forms. English: "Beacon position". */
#define TR_F_BEACON_POSITION_2 "Posizione beacon"
/** Italian text for the form label for the "beacon via internet" field or fieldset, rendered on the configuration forms. English: "Beacon via Internet". */
#define TR_F_BEACON_VIA_INTERNET "Beacon via Internet"
/** Italian text for the form label for the "beacon via rf" field or fieldset, rendered on the configuration forms. English: "Beacon via RF". */
#define TR_F_BEACON_VIA_RF "Beacon via RF"
/** Italian text for the form label for the "comment" field or fieldset, rendered on the configuration forms. English: "Comment". */
#define TR_F_COMMENT "Commento"
/** Italian text for the form label for the "compress position" field or fieldset, rendered on the configuration forms. English: "Compress position". */
#define TR_F_COMPRESS_POSITION "Comprimi posizione"
/** Italian text for the form label for the "dashboard" field or fieldset, rendered on the configuration forms. English: "Dashboard". */
#define TR_F_DASHBOARD "Pannello"
/** Italian text for the form label for the "data interval s" field or fieldset, rendered on the configuration forms. English: "Data interval (s)". */
#define TR_F_DATA_INTERVAL_S "Intervallo dati (s)"
/** Italian text for the form label for the "digipeater" field or fieldset, rendered on the configuration forms. English: "Digipeater". */
#define TR_F_DIGIPEATER "Digipeater"
/** Italian text for the form label for the "enable" field or fieldset, rendered on the configuration forms. English: "Enable". */
#define TR_F_ENABLE "Abilita"
/** Italian text for the form label for the "enable digipeater" field or fieldset, rendered on the configuration forms. English: "Enable Digipeater". */
#define TR_F_ENABLE_DIGIPEATER "Abilita Digipeater"
/** Italian text for the form label for the "enable igate" field or fieldset, rendered on the configuration forms. English: "Enable IGate". */
#define TR_F_ENABLE_IGATE "Abilita IGate"
/** Italian text for the form label for the "enable messaging" field or fieldset, rendered on the configuration forms. English: "Enable messaging". */
#define TR_F_ENABLE_MESSAGING "Abilita messaggistica"
/** Italian text for the form label for the "enable tracker" field or fieldset, rendered on the configuration forms. English: "Enable Tracker". */
#define TR_F_ENABLE_TRACKER "Abilita Tracker"
/** Italian text for the form label for the "enable wx" field or fieldset, rendered on the configuration forms. English: "Enable WX". */
#define TR_F_ENABLE_WX "Abilita WX"
/** Italian text for the form label for the "file storage" field or fieldset, rendered on the configuration forms. English: "File Storage". */
#define TR_F_FILE_STORAGE "Archiviazione file"
/** Italian text for the form label for the "filter" field or fieldset, rendered on the configuration forms. English: "Filter". */
#define TR_F_FILTER "Filtro"
/** Italian text for the form label for the "fixed altitude m" field or fieldset, rendered on the configuration forms. English: "Fixed Altitude (m)". */
#define TR_F_FIXED_ALTITUDE_M "Altitudine fissa (m)"
/** Italian text for the form label for the "fixed interval s" field or fieldset, rendered on the configuration forms. English: "Fixed interval (s)". */
#define TR_F_FIXED_INTERVAL_S "Intervallo fisso (s)"
/** Italian text for the form label for the "fixed latitude" field or fieldset, rendered on the configuration forms. English: "Fixed Latitude". */
#define TR_F_FIXED_LATITUDE "Latitudine fissa"
/** Italian text for the form label for the "fixed longitude" field or fieldset, rendered on the configuration forms. English: "Fixed Longitude". */
#define TR_F_FIXED_LONGITUDE "Longitudine fissa"
/** Italian text for the form label for the "fx 25 forward error corrected ax 25" field or fieldset, rendered on the configuration forms. English: "FX.25
 * (forward-error-corrected AX.25)". */
#define TR_F_FX_25_FORWARD_ERROR_CORRECTED_AX_25 "FX.25 (AX.25 con correzione d'errore)"
/** Italian text for the form label for the "igate" field or fieldset, rendered on the configuration forms. English: "IGate". */
#define TR_F_IGATE "IGate"
/** Italian text for the form label for the "include altitude" field or fieldset, rendered on the configuration forms. English: "Include altitude". */
#define TR_F_INCLUDE_ALTITUDE "Includi altitudine"
/** Form label for the "tracker phg" field or fieldset, rendered on the Tracker page. */
#define TR_F_TRACKER_PHG "Includi estensione dati PHG"
/** Italian text for the form label for the "internet to rf" field or fieldset, rendered on the configuration forms. English: "Internet to RF". */
#define TR_F_INTERNET_TO_RF "Internet verso RF"
/** Italian text for the form label for the "latitude" field or fieldset, rendered on the configuration forms. English: "Latitude". */
#define TR_F_LATITUDE "Latitudine"
/** Italian text for the form label for the "longitude" field or fieldset, rendered on the configuration forms. English: "Longitude". */
#define TR_F_LONGITUDE "Longitudine"
/** Italian text for the form label for the "message" field or fieldset, rendered on the configuration forms. English: "Message". */
#define TR_F_MESSAGE "Messaggio"
/** Italian text for the form label for the "message alarm enable" field or fieldset, rendered on the configuration forms. English: "Enable Message Alarm". */
#define TR_F_MESSAGE_ALARM_ENABLE "Abilita allarme messaggi"
/** Italian text for the form label for the "message alarm pin" field or fieldset, rendered on the configuration forms. English: "Message Alarm pin". */
#define TR_F_MESSAGE_ALARM_PIN "Pin allarme messaggi"
/** Italian text for the fieldset label for the operator-defined message-group name slots on the Message page. English: "Message Groups". */
#define TR_F_MESSAGE_GROUPS "Gruppi messaggi"
/** Italian text for the form label for one operator-defined message-group name slot, "%d" is the slot number (1-based). English: "Group %d". */
#define TR_F_MESSAGE_GROUP_FMT "Gruppo %d"
/** Italian text for the option label for the Mic-E position comment M0 (Off Duty), rendered on the Tracker page. English: "M0 Off Duty". */
#define TR_F_MICE_MSG_M0 "M0 Fuori servizio"
/** Italian text for the option label for the Mic-E position comment M1 (En Route), rendered on the Tracker page. English: "M1 En Route". */
#define TR_F_MICE_MSG_M1 "M1 In viaggio"
/** Italian text for the option label for the Mic-E position comment M2 (In Service), rendered on the Tracker page. English: "M2 In Service". */
#define TR_F_MICE_MSG_M2 "M2 In servizio"
/** Italian text for the option label for the Mic-E position comment M3 (Returning), rendered on the Tracker page. English: "M3 Returning". */
#define TR_F_MICE_MSG_M3 "M3 In rientro"
/** Italian text for the option label for the Mic-E position comment M4 (Committed), rendered on the Tracker page. English: "M4 Committed". */
#define TR_F_MICE_MSG_M4 "M4 Impegnato"
/** Italian text for the option label for the Mic-E position comment M5 (Special), rendered on the Tracker page. English: "M5 Special". */
#define TR_F_MICE_MSG_M5 "M5 Speciale"
/** Italian text for the option label for the Mic-E position comment M6 (Priority), rendered on the Tracker page. English: "M6 Priority". */
#define TR_F_MICE_MSG_M6 "M6 Prioritario"
/** Italian text for the word prefixed to the number of each locally defined Mic-E position comment C0-C6, rendered on the Tracker page. English: "Custom". */
#define TR_F_MICE_MSG_CUSTOM "Personalizzato"
/** Italian text for the form label for the "mice position" field or fieldset, rendered on the configuration forms. English: "Mic-E position encoding". */
#define TR_F_MICE_POSITION "Codifica posizione Mic-E"
/** Italian text for the form label for the "mice position comment" field or fieldset, rendered on the configuration forms. English: "Mic-E position comment".
 */
#define TR_F_MICE_POSITION_COMMENT "Commento di posizione Mic-E"
/** Italian text for the form label for the "mode" field or fieldset, rendered on the configuration forms. English: "Mode". */
#define TR_F_MODE "Modalità"
/** Italian text for the form label for the "my callsign" field or fieldset, rendered on the configuration forms. English: "My Callsign". */
#define TR_F_MY_CALLSIGN "Mio nominativo"
/** Italian text for the caption of the control that copies the station identity into the current form. English: "Use My Station Data". */
#define TR_USE_MY_STATION_DATA "Usa i dati della mia stazione"
/** Italian text for the form label for the "name" field or fieldset, rendered on the configuration forms. English: "Name". */
#define TR_F_NAME "Nome"
/** Italian text for the form label for the "object item name" field or fieldset, rendered on the configuration forms. English: "Object/Item name". */
#define TR_F_OBJECT_ITEM_NAME "Nome oggetto/item"
/** Italian text for the form label for the "object name" field or fieldset, rendered on the configuration forms. English: "Object name". */
#define TR_F_OBJECT_NAME "Nome oggetto"
/** Italian text for the form label for the "options" field or fieldset, rendered on the configuration forms. English: "Options". */
#define TR_F_OPTIONS "Opzioni"
/** Italian text for the form label for the "parm unit eqns interval s" field or fieldset, rendered on the configuration forms. English: "PARM/UNIT/EQNS
 * interval (s)". */
#define TR_F_PARM_UNIT_EQNS_INTERVAL_S "Intervallo PARM/UNIT/EQNS (s)"
/** Italian text for the form label for the "password" field or fieldset, rendered on the configuration forms. English: "Password". */
#define TR_F_PASSWORD "Password"
/** Italian text for the form label for the "position" field or fieldset, rendered on the configuration forms. English: "Position". */
#define TR_F_POSITION "Posizione"
/** Italian text for the form label for the "preamble ms" field or fieldset, rendered on the configuration forms. English: "Preamble (ms)". */
#define TR_F_PREAMBLE_MS "Preambolo (ms)"
/** Italian text for the selector entry meaning the feature or pin is switched off. English: "Disabled". */
#define TR_DISABLED "Disabilitato"
/** Italian text for the format string for a GPIO selector entry that is already claimed, taking the pin number and the claiming peripheral. English: "GPIO%d
 * (used: %.30s)". */
#define TR_GPIO_USED_BY "GPIO%d (utilizzato: %.30s)"
/** Italian text for the form label for the "protocol" field or fieldset, rendered on the configuration forms. English: "Protocol". */
#define TR_F_PROTOCOL "Protocollo"
/** Italian text for the form label for the "query" field or fieldset, rendered on the configuration forms. English: "Query". */
#define TR_F_QUERY "Interrogazione"
/** Italian text for the form label for the "enable query" field or fieldset, rendered on the configuration forms. English: "Enable query responder". */
#define TR_F_ENABLE_QUERY "Abilita risponditore interrogazioni"
/** Italian text for the form label for the "query rf" field or fieldset, rendered on the configuration forms. English: "Answer queries heard on RF". */
#define TR_F_QUERY_RF "Rispondere alle interrogazioni ricevute in RF"
/** Italian text for the form label for the "query inet" field or fieldset, rendered on the configuration forms. English: "Answer queries heard from APRS-IS".
 */
#define TR_F_QUERY_INET "Rispondere alle interrogazioni ricevute da APRS-IS"
/** Italian text for the form label for the "query aprs" field or fieldset, rendered on the configuration forms. English: "?APRS? - general station query". */
#define TR_F_QUERY_APRS "?APRS? - interrogazione generale stazione"
/** Italian text for the form label for the "query wx" field or fieldset, rendered on the configuration forms. English: "?WX? - weather report request". */
#define TR_F_QUERY_WX "?WX? - richiesta rapporto meteo"
/** Italian text for the form label for the "query igate" field or fieldset, rendered on the configuration forms. English: "?IGATE? - IGate status request". */
#define TR_F_QUERY_IGATE "?IGATE? - richiesta stato IGate"
/** Italian text for the form label for the "query directed" field or fieldset, rendered on the configuration forms. English: "Directed queries (CALL:?query?)".
 */
#define TR_F_QUERY_DIRECTED "Interrogazioni dirette (CALL:?query?)"
/** Italian text for the form label for the "query ext" field or fieldset, rendered on the configuration forms. English: "Extended directed queries
 * (?APRSD/?APRSH/?APRSM/?APRSO/?APRSP/?APRSS/?APRST)". */
#define TR_F_QUERY_EXT "Interrogazioni dirette estese (?APRSD/?APRSH/?APRSM/?APRSO/?APRSP/?APRSS/?APRST)"
/** Italian text for the form label for the "query min interval" field or fieldset, rendered on the configuration forms. English: "Minimum seconds between
 * identical responses". */
#define TR_F_QUERY_MIN_INTERVAL "Secondi minimi tra risposte identiche"
/** Italian text for the form label for the "radio modem" field or fieldset, rendered on the configuration forms. English: "Radiomodem". */
#define TR_F_RADIO_MODEM "Radiomodem"
/** Italian text for the form label for the "retry count" field or fieldset, rendered on the configuration forms. English: "Retry count". */
#define TR_F_RETRY_COUNT "Numero tentativi"
/** Italian text for the form label for the "retry interval s" field or fieldset, rendered on the configuration forms. English: "Retry interval (s)". */
#define TR_F_RETRY_INTERVAL_S "Intervallo tentativi (s)"
/** Italian text for the form label for the "rf to internet" field or fieldset, rendered on the configuration forms. English: "RF to Internet". */
#define TR_F_RF_TO_INTERNET "RF verso Internet"
/** Italian text for the form label for the "rf tx buffers" field or fieldset, rendered on the configuration forms. English: "TX buffers". */
#define TR_F_RF_TX_BUFFERS "Buffer TX"
/** Italian text for the form label for the "duty cycle en" field or fieldset, rendered on the configuration forms. English: "Duty-cycle limiter". */
#define TR_F_DUTY_CYCLE_EN "Limitatore duty cycle"
/** Italian text for the form label for the "duty cycle pct" field or fieldset, rendered on the configuration forms. English: "Duty-cycle limit (%)". */
#define TR_F_DUTY_CYCLE_PCT "Limite duty cycle (%)"
/** Italian text for the form label for the "ptt min unkey ms" field or fieldset, rendered on the configuration forms. English: "PTT minimum unkey time (ms)".
 */
#define TR_F_PTT_MIN_UNKEY_MS "Tempo minimo PTT sbloccato (ms)"
/** Italian text for the form label for the "csma persistence" field or fieldset, rendered on the configuration forms. English: "CSMA persistence (p, 1-255)".
 */
#define TR_F_CSMA_PERSISTENCE "Persistenza CSMA (p, 1-255)"
/** Italian text for the form label for the "send receive via internet" field or fieldset, rendered on the configuration forms. English: "Send/receive via
 * Internet". */
#define TR_F_SEND_RECEIVE_VIA_INTERNET "Invia/ricevi via Internet"
/** Italian text for the form label for the "send receive via rf" field or fieldset, rendered on the configuration forms. English: "Send/receive via RF". */
#define TR_F_SEND_RECEIVE_VIA_RF "Invia/ricevi via RF"
/** Italian text for the form label for the "send via internet" field or fieldset, rendered on the configuration forms. English: "Send via Internet". */
#define TR_F_SEND_VIA_INTERNET "Invia via Internet"
/** Italian text for the form label for the "send via rf" field or fieldset, rendered on the configuration forms. English: "Send via RF". */
#define TR_F_SEND_VIA_RF "Invia via RF"
/** Italian text for the form label for the "sensor mapping enable averaged source channel" field or fieldset, rendered on the configuration forms. English:
 * "Sensor Mapping (enable / averaged / source channel)". */
#define TR_F_SENSOR_MAPPING_ENABLE_AVERAGED_SOURCE_CHANNEL "Mappatura sensori (abilita / media / canale sorgente)"
/** Italian text for the form label for the "server host" field or fieldset, rendered on the configuration forms. English: "Server Host". */
#define TR_F_SERVER_HOST "Host server"
/** Italian text for the form label for the "server port" field or fieldset, rendered on the configuration forms. English: "Server Port". */
#define TR_F_SERVER_PORT "Porta server"
/** Italian text for the form label for the "ssid" field or fieldset, rendered on the configuration forms. English: "SSID". */
#define TR_F_SSID "SSID"
/** Italian text for the form label for the "station" field or fieldset, rendered on the configuration forms. English: "Station". */
#define TR_F_STATION "Stazione"
/** Italian text for the form label for the "bulletins" field or fieldset, rendered on the configuration forms. English: "Bulletins". */
#define TR_F_BULLETINS "Bollettini"
/** Italian text for the form label for the "bulletin fmt" field or fieldset, rendered on the configuration forms. English: "Bulletin %d". */
#define TR_F_BULLETIN_FMT "Bollettino %d"
/** Italian text for the form label for the "bulletin id" field or fieldset, rendered on the configuration forms. English: "Identifier (0-9 bulletin, A-Z
 * announcement)". */
#define TR_F_BULLETIN_ID "Identificatore (0-9 bollettino, A-Z annuncio)"
/** Italian text for the form label for the "bulletin group" field or fieldset, rendered on the configuration forms. English: "Group (up to 5 chars, empty =
 * general)". */
#define TR_F_BULLETIN_GROUP "Gruppo (fino a 5 caratteri, vuoto = generale)"
/** Italian text for the form label for the "bulletin msg" field or fieldset, rendered on the configuration forms. English: "Message (max 67 chars)". */
#define TR_F_BULLETIN_MSG "Messaggio (max 67 caratteri)"
/** Italian text for the form label for the "bulletin expire" field or fieldset, rendered on the configuration forms. English: "Expire (hours, 0 = never)". */
#define TR_F_BULLETIN_EXPIRE "Scadenza (ore, 0 = mai)"
/** Italian text for the form label for the "objitems" field or fieldset, rendered on the configuration forms. English: "Objects and Items". */
#define TR_F_OBJITEMS "Oggetti e Item"
/** Italian text for the form label for the "objitem fmt" field or fieldset, rendered on the configuration forms. English: "Object/Item %d". */
#define TR_F_OBJITEM_FMT "Oggetto/Item %d"
/** Italian text for the form label for the "objitem tx control" field or fieldset, rendered on the configuration forms. English: "Transmission Control". */
#define TR_F_OBJITEM_TX_CONTROL "Controllo Trasmissione"
/** Italian text for the form label for the "objitem identity" field or fieldset, rendered on the configuration forms. English: "Identity & State". */
#define TR_F_OBJITEM_IDENTITY "Identità e Stato"
/** Italian text for the form label for the "objitem pos symbol" field or fieldset, rendered on the configuration forms. English: "Position & Symbol". */
#define TR_F_OBJITEM_POS_SYMBOL "Posizione e Simbolo"
/** Italian text for the form label for the "objitem area section" field or fieldset, rendered on the configuration forms. English: "Area Object". */
#define TR_F_OBJITEM_AREA_SECTION "Oggetto Area"
/** Italian text for the form label for the "objitem signpost section" field or fieldset, rendered on the configuration forms. English: "Signpost". */
#define TR_F_OBJITEM_SIGNPOST_SECTION "Cartello (Signpost)"
/** Italian text for the form label for the "objitem repeater section" field or fieldset, rendered on the configuration forms. English: "Repeater Radio
 * Parameters". */
#define TR_F_OBJITEM_REPEATER_SECTION "Parametri Radio del Ripetitore"
/** Italian text for the form label for the "objitem timing section" field or fieldset, rendered on the configuration forms. English: "Beacon Timing". */
#define TR_F_OBJITEM_TIMING_SECTION "Temporizzazione Beacon"
/** Italian text for the form label for the "objitem type" field or fieldset, rendered on the configuration forms. English: "Type". */
#define TR_F_OBJITEM_TYPE "Tipo"
/** Italian text for the form label for the "objitem type object" field or fieldset, rendered on the configuration forms. English: "Object (timestamped)". */
#define TR_F_OBJITEM_TYPE_OBJECT "Oggetto (con timestamp)"
/** Italian text for the form label for the "objitem type item" field or fieldset, rendered on the configuration forms. English: "Item (non-timestamped)". */
#define TR_F_OBJITEM_TYPE_ITEM "Item (senza timestamp)"
/** Italian text for the form label for the "objitem permanent" field or fieldset, rendered on the configuration forms. English: "Permanent (Object only,
 * 111111z)". */
#define TR_F_OBJITEM_PERMANENT "Permanente (solo Oggetto, 111111z)"
/** Italian text for the form label for the "objitem permanent note" field or fieldset, rendered on the configuration forms. English: "A permanent Object is
 * sent with the fixed 111111z timestamp instead of the live time, s...". */
#define TR_F_OBJITEM_PERMANENT_NOTE                                                                                                                            \
    "Un Oggetto permanente viene inviato con il timestamp fisso 111111z invece dell'ora corrente, cosi non viene mai sostituito da un Oggetto omonimo di "     \
    "un'altra stazione - solo la stazione di origine puo aggiornarlo o spostarlo. Non ha effetto su un Item."
/** Italian text for the form label for the "objitem active" field or fieldset, rendered on the configuration forms. English: "Active (uncheck = kill)". */
#define TR_F_OBJITEM_ACTIVE "Attivo (deseleziona = elimina)"
/** Italian text for the form label for the "objitem scope" field or fieldset, rendered on the configuration forms. English: "Scope". */
#define TR_F_OBJITEM_SCOPE "Ambito"
/** Italian text for the form label for the "objitem scope private" field or fieldset, rendered on the configuration forms. English: "Private (not
 * transmitted)". */
#define TR_F_OBJITEM_SCOPE_PRIVATE "Privato (non trasmesso)"
/** Italian text for the form label for the "objitem scope local" field or fieldset, rendered on the configuration forms. English: "Local (RF only)". */
#define TR_F_OBJITEM_SCOPE_LOCAL "Locale (solo RF)"
/** Italian text for the form label for the "objitem scope global" field or fieldset, rendered on the configuration forms. English: "Global (RF + Internet)". */
#define TR_F_OBJITEM_SCOPE_GLOBAL "Globale (RF + Internet)"
/** Italian text for the form label for the "objitem symbol" field or fieldset, rendered on the configuration forms. English: "Symbol / overlay". */
#define TR_F_OBJITEM_SYMBOL "Simbolo / overlay"
/** Italian text for the form label for the "objitem course" field or fieldset, rendered on the configuration forms. English: "Course (deg, 0-359)". */
#define TR_F_OBJITEM_COURSE "Rotta (gradi, 0-359)"
/** Italian text for the form label for the "objitem speed" field or fieldset, rendered on the configuration forms. English: "Speed (knots, 0 = omit)". */
#define TR_F_OBJITEM_SPEED "Velocità (nodi, 0 = ometti)"
/** Italian text for the form label for the "objitem area shape" field or fieldset, rendered on the configuration forms. English: "Area shape (\l symbol)". */
#define TR_F_OBJITEM_AREA_SHAPE "Forma area (simbolo \\l)"
/** Italian text for the form label for the "objitem shape circle" field or fieldset, rendered on the configuration forms. English: "Circle". */
#define TR_F_OBJITEM_SHAPE_CIRCLE "Cerchio"
/** Italian text for the form label for the "objitem shape line" field or fieldset, rendered on the configuration forms. English: "Line". */
#define TR_F_OBJITEM_SHAPE_LINE "Linea"
/** Italian text for the form label for the "objitem shape ellipse" field or fieldset, rendered on the configuration forms. English: "Ellipse". */
#define TR_F_OBJITEM_SHAPE_ELLIPSE "Ellisse"
/** Italian text for the form label for the "objitem shape triangle" field or fieldset, rendered on the configuration forms. English: "Triangle". */
#define TR_F_OBJITEM_SHAPE_TRIANGLE "Triangolo"
/** Italian text for the form label for the "objitem shape box" field or fieldset, rendered on the configuration forms. English: "Box". */
#define TR_F_OBJITEM_SHAPE_BOX "Rettangolo"
/** Italian text for the form label for the "objitem shape filled" field or fieldset, rendered on the configuration forms. English: "(filled)". */
#define TR_F_OBJITEM_SHAPE_FILLED " (pieno)"
/** Italian text for the form label for the "objitem area color" field or fieldset, rendered on the configuration forms. English: "Area color (0-15)". */
#define TR_F_OBJITEM_AREA_COLOR "Colore area (0-15)"
/** Italian text for the form label for the "objitem area lat off" field or fieldset, rendered on the configuration forms. English: "Area latitude offset
 * (deg)". */
#define TR_F_OBJITEM_AREA_LAT_OFF "Offset latitudine area (gradi)"
/** Italian text for the form label for the "objitem area lon off" field or fieldset, rendered on the configuration forms. English: "Area longitude offset
 * (deg)". */
#define TR_F_OBJITEM_AREA_LON_OFF "Offset longitudine area (gradi)"
/** Italian text for the form label for the "objitem signpost" field or fieldset, rendered on the configuration forms. English: "Signpost text (\m symbol, 3
 * chars)". */
#define TR_F_OBJITEM_SIGNPOST "Testo segnale (simbolo \\m, 3 caratteri)"
/** Italian text for the form label for the "objitem freq" field or fieldset, rendered on the configuration forms. English: "Monitor frequency (MHz, 0 = none)".
 */
#define TR_F_OBJITEM_FREQ "Frequenza di monitoraggio (MHz, 0 = nessuna)"
/** Italian text for the form label for the "objitem duplex" field or fieldset, rendered on the configuration forms. English: "Duplex direction". */
#define TR_F_OBJITEM_DUPLEX "Direzione duplex"
/** Italian text for the form label for the "objitem duplex simplex" field or fieldset, rendered on the configuration forms. English: "Simplex". */
#define TR_F_OBJITEM_DUPLEX_SIMPLEX "Simplex"
/** Italian text for the form label for the "objitem duplex plus" field or fieldset, rendered on the configuration forms. English: "Plus (+)". */
#define TR_F_OBJITEM_DUPLEX_PLUS "Positivo (+)"
/** Italian text for the form label for the "objitem duplex minus" field or fieldset, rendered on the configuration forms. English: "Minus (-)". */
#define TR_F_OBJITEM_DUPLEX_MINUS "Negativo (-)"
/** Italian text for the form label for the "objitem offset" field or fieldset, rendered on the configuration forms. English: "Duplex offset (kHz)". */
#define TR_F_OBJITEM_OFFSET "Offset duplex (kHz)"
/** Italian text for the form label for the "objitem tone" field or fieldset, rendered on the configuration forms. English: "Subaudible tone CTCSS (Hz, 0 =
 * none)". */
#define TR_F_OBJITEM_TONE "Tono subaudio CTCSS (Hz, 0 = nessuno)"
/** Italian text for the form label for the "objitem range" field or fieldset, rendered on the configuration forms. English: "Coverage range (0 = none)". */
#define TR_F_OBJITEM_RANGE "Portata di copertura (0 = nessuna)"
/** Italian text for the form label for the "objitem range unit" field or fieldset, rendered on the configuration forms. English: "Range unit". */
#define TR_F_OBJITEM_RANGE_UNIT "Unità di portata"
/** Italian text for the form label for the "objitem range unit mi" field or fieldset, rendered on the configuration forms. English: "Miles". */
#define TR_F_OBJITEM_RANGE_UNIT_MI "Miglia"
/** Italian text for the form label for the "objitem range unit km" field or fieldset, rendered on the configuration forms. English: "Kilometers". */
#define TR_F_OBJITEM_RANGE_UNIT_KM "Chilometri"
/** Italian text for the form label for the "objitem path fmt" field or fieldset, rendered on the configuration forms. English: "Path %d". */
#define TR_F_OBJITEM_PATH_FMT "Percorso %d"
/** Italian text for the form label for the "objitem qru" field or fieldset, rendered on the configuration forms. English: "QRU group membership". */
#define TR_F_OBJITEM_QRU "Appartenenza gruppo QRU"
/** Italian text for the form label for the "objitem qru none" field or fieldset, rendered on the configuration forms. English: "(none)". */
#define TR_F_OBJITEM_QRU_NONE "(nessuno)"
/** Italian text for the form label for the "qru ambu" field or fieldset, rendered on the configuration forms. English: "ambulance". */
#define TR_F_QRU_AMBU "ambulanza"
/** Italian text for the form label for the "qru club" field or fieldset, rendered on the configuration forms. English: "ham radio club". */
#define TR_F_QRU_CLUB "club di radioamatori"
/** Italian text for the form label for the "qru echo" field or fieldset, rendered on the configuration forms. English: "Echolink". */
#define TR_F_QRU_ECHO "Echolink"
/** Italian text for the form label for the "qru fire" field or fieldset, rendered on the configuration forms. English: "fire station". */
#define TR_F_QRU_FIRE "caserma dei pompieri"
/** Italian text for the form label for the "qru food" field or fieldset, rendered on the configuration forms. English: "restaurants". */
#define TR_F_QRU_FOOD "ristoranti"
/** Italian text for the form label for the "qru fuel" field or fieldset, rendered on the configuration forms. English: "gas/petrol stations". */
#define TR_F_QRU_FUEL "stazioni di servizio/benzinai"
/** Italian text for the form label for the "qru hosp" field or fieldset, rendered on the configuration forms. English: "hospitals". */
#define TR_F_QRU_HOSP "ospedali"
/** Italian text for the form label for the "qru lifeboat" field or fieldset, rendered on the configuration forms. English: "lifeboats". */
#define TR_F_QRU_LIFEBOAT "scialuppe di salvataggio"
/** Italian text for the form label for the "qru lths" field or fieldset, rendered on the configuration forms. English: "lighthouses". */
#define TR_F_QRU_LTHS "fari"
/** Italian text for the form label for the "qru poli" field or fieldset, rendered on the configuration forms. English: "police stations". */
#define TR_F_QRU_POLI "stazioni di polizia"
/** Italian text for the form label for the "qru post" field or fieldset, rendered on the configuration forms. English: "post offices". */
#define TR_F_QRU_POST "uffici postali"
/** Italian text for the form label for the "qru rd13" field or fieldset, rendered on the configuration forms. English: "D-Star 13cm repeaters". */
#define TR_F_QRU_RD13 "ripetitori D-Star 13cm"
/** Italian text for the form label for the "qru rd23" field or fieldset, rendered on the configuration forms. English: "D-Star 23cm repeaters". */
#define TR_F_QRU_RD23 "ripetitori D-Star 23cm"
/** Italian text for the form label for the "qru rd2m" field or fieldset, rendered on the configuration forms. English: "D-Star 2m repeaters". */
#define TR_F_QRU_RD2M "ripetitori D-Star 2m"
/** Italian text for the form label for the "qru rd3c" field or fieldset, rendered on the configuration forms. English: "D-Star 3cm repeaters". */
#define TR_F_QRU_RD3C "ripetitori D-Star 3cm"
/** Italian text for the form label for the "qru rd70" field or fieldset, rendered on the configuration forms. English: "D-Star 70cm repeaters". */
#define TR_F_QRU_RD70 "ripetitori D-Star 70cm"
/** Italian text for the form label for the "qru rp10" field or fieldset, rendered on the configuration forms. English: "analog 10m repeaters". */
#define TR_F_QRU_RP10 "ripetitori analogici 10m"
/** Italian text for the form label for the "qru rp13" field or fieldset, rendered on the configuration forms. English: "analog 13cm repeaters". */
#define TR_F_QRU_RP13 "ripetitori analogici 13cm"
/** Italian text for the form label for the "qru rp23" field or fieldset, rendered on the configuration forms. English: "analog 23cm repeaters". */
#define TR_F_QRU_RP23 "ripetitori analogici 23cm"
/** Italian text for the form label for the "qru rp2m" field or fieldset, rendered on the configuration forms. English: "analog 2m repeaters". */
#define TR_F_QRU_RP2M "ripetitori analogici 2m"
/** Italian text for the form label for the "qru rp3c" field or fieldset, rendered on the configuration forms. English: "analog 3cm repeaters". */
#define TR_F_QRU_RP3C "ripetitori analogici 3cm"
/** Italian text for the form label for the "qru rp6m" field or fieldset, rendered on the configuration forms. English: "analog 6m repeaters". */
#define TR_F_QRU_RP6M "ripetitori analogici 6m"
/** Italian text for the form label for the "qru rp70" field or fieldset, rendered on the configuration forms. English: "analog 70cm repeaters". */
#define TR_F_QRU_RP70 "ripetitori analogici 70cm"
/** Italian text for the form label for the "qru rt13" field or fieldset, rendered on the configuration forms. English: "television 13cm repeaters". */
#define TR_F_QRU_RT13 "ripetitori televisivi 13cm"
/** Italian text for the form label for the "qru rt23" field or fieldset, rendered on the configuration forms. English: "television 23cm repeaters". */
#define TR_F_QRU_RT23 "ripetitori televisivi 23cm"
/** Italian text for the form label for the "qru rt3c" field or fieldset, rendered on the configuration forms. English: "television 3cm repeaters". */
#define TR_F_QRU_RT3C "ripetitori televisivi 3cm"
/** Italian text for the form label for the "qru srail" field or fieldset, rendered on the configuration forms. English: "steam railroad". */
#define TR_F_QRU_SRAIL "ferrovia a vapore"
/** Italian text for the form label for the "qru stor" field or fieldset, rendered on the configuration forms. English: "Amateur Radio stores". */
#define TR_F_QRU_STOR "negozi di radioamatori"
/** Italian text for the form label for the "qru t2srv" field or fieldset, rendered on the configuration forms. English: "approx. locations of Tier 2 APRS-IS
 * servers". */
#define TR_F_QRU_T2SRV "posizioni approx. dei server APRS-IS Tier 2"
/** Italian text for the form label for the "qru vete" field or fieldset, rendered on the configuration forms. English: "veterinarians". */
#define TR_F_QRU_VETE "veterinari"
/** Italian text for the form label for the "qru wota" field or fieldset, rendered on the configuration forms. English: "Wainwrights On The Air". */
#define TR_F_QRU_WOTA "Wainwrights On The Air"
/** Italian text for the form label for the "objitem init rate" field or fieldset, rendered on the configuration forms. English: "Initial repeat rate (s)". */
#define TR_F_OBJITEM_INIT_RATE "Intervallo iniziale (s)"
/** Italian text for the form label for the "objitem slow rate" field or fieldset, rendered on the configuration forms. English: "Slow repeat rate (s, 0 = no
 * decay)". */
#define TR_F_OBJITEM_SLOW_RATE "Intervallo lento (s, 0 = nessun decadimento)"
/** Italian text for the form label for the "objitem decay" field or fieldset, rendered on the configuration forms. English: "Decay ratio (e.g. 2.0, <1 =
 * none)". */
#define TR_F_OBJITEM_DECAY "Rapporto di decadimento (es. 2.0, <1 = nessuno)"
/** Italian text for the explanatory note shown beside the objitem setting, rendered on the configuration forms. English: "Objects are timestamped (;NAME);
 * Items are never timestamped ()NAME). Unchecking Active...". */
#define TR_NOTE_OBJITEM                                                                                                                                        \
    "Gli Oggetti hanno un timestamp (;NOME); gli Item non lo hanno mai ()NOME). Deselezionando Attivo si inviano report di eliminazione, poi si disabilita "   \
    "automaticamente. L'Ambito limita la trasmissione indipendentemente dalle caselle RF/Internet."
/** Italian text for the form label for the "status beacon" field or fieldset, rendered on the configuration forms. English: "Status Beacon". */
#define TR_F_STATUS_BEACON "Beacon di stato"
/** Italian text for the form label for the "status interval s 0 off" field or fieldset, rendered on the configuration forms. English: "Status interval (s,
 * 0=off)". */
#define TR_F_STATUS_INTERVAL_S_0_OFF "Intervallo stato (s, 0=off)"
/** Italian text for the form label for the "status text" field or fieldset, rendered on the configuration forms. English: "Status text". */
#define TR_F_STATUS_TEXT "Testo di stato"
/** Italian text for the form label for the "system" field or fieldset, rendered on the configuration forms. English: "System". */
#define TR_F_SYSTEM "Sistema"
/** Italian text for the form label for the "telemetry" field or fieldset, rendered on the configuration forms. English: "Telemetry". */
#define TR_F_TELEMETRY "Telemetria"
/** Italian text for the form label for the "beacon" field or fieldset, rendered on the configuration forms. English: "Beacon". */
#define TR_F_BEACON "Beacon"
/** Italian text for the form label for the "tracker" field or fieldset, rendered on the configuration forms. English: "Tracker". */
#define TR_F_TRACKER "Tracker"
/** Italian text for the form label for the "tx time slot ms" field or fieldset, rendered on the configuration forms. English: "TX time-slot (ms)". */
#define TR_F_TX_TIME_SLOT_MS "Slot temporale TX (ms)"
/** Italian text for the form label for the "upload" field or fieldset, rendered on the configuration forms. English: "Upload". */
#define TR_F_UPLOAD "Carica"
/** Italian text for the form label for the "username" field or fieldset, rendered on the configuration forms. English: "Username". */
#define TR_F_USERNAME "Nome utente"
/** Italian text for the form label for the "weather" field or fieldset, rendered on the configuration forms. English: "Weather". */
#define TR_F_WEATHER "Meteo"
/** Italian text for the form label for the "weather station" field or fieldset, rendered on the configuration forms. English: "Weather Station". */
#define TR_F_WEATHER_STATION "Stazione meteo"
/** Italian text for the form label for the "wireless" field or fieldset, rendered on the configuration forms. English: "Wireless". */
#define TR_F_WIRELESS "Senza fili"

/** Italian text for the form label for the "off" field or fieldset, rendered on the configuration forms. English: "Off". */
#define TR_F_OFF "Off"

/** @} */

/**
 * @name page_common.c: dashboard / sysinfo
 * @{
 */
/** Italian text for the interface string labelling enabled, rendered on the dashboard. English: "enabled". */
#define TR_ENABLED "abilitato"
/** Italian text for the dashboard label or value for digi short, rendered on the dashboard. English: "Digi". */
#define TR_DASH_DIGI_SHORT "Digi"
/** Italian text for the dashboard label or value for wx short, rendered on the dashboard. English: "WX:". */
#define TR_DASH_WX_SHORT "WX:"
/** Italian text for the dashboard label or value for datetime, rendered on the dashboard. English: "Date/Time:". */
#define TR_DASH_DATETIME "Data/Ora:"
/** Italian text for the dashboard label or value for uptime, rendered on the dashboard. English: "Uptime:". */
#define TR_DASH_UPTIME "Tempo di attività:"
/** Italian text for the dashboard label or value for free heap, rendered on the dashboard. English: "Free heap:". */
#define TR_DASH_FREE_HEAP "Heap libero:"
/** Italian text for the dashboard label or value for littlefs, rendered on the dashboard. English: "LittleFS:". */
#define TR_DASH_LITTLEFS "LittleFS:"
/** Italian text for the dashboard label or value for sysinfo, rendered on the dashboard. English: "System Info". */
#define TR_DASH_SYSINFO "Informazioni sistema"
/** Italian text for the dashboard label or value for igate traffic, rendered on the dashboard. English: "IGate Traffic". */
#define TR_DASH_IGATE_TRAFFIC "Traffico IGate"
/** Italian text for the traffic-log column heading or label for pause, rendered on the dashboard. English: "Pause". */
#define TR_TRAFFIC_PAUSE "Pausa"
/** Italian text for the traffic-log column heading or label for resume, rendered on the dashboard. English: "Resume". */
#define TR_TRAFFIC_RESUME "Riprendi"
/** Italian text for the traffic-log column heading or label for clear, rendered on the dashboard. English: "Clear". */
#define TR_TRAFFIC_CLEAR "Cancella"
/** Italian text for the traffic-log column heading or label for waiting, rendered on the dashboard. English: "Waiting for traffic...". */
#define TR_TRAFFIC_WAITING "In attesa di traffico..."
/** Italian text for the traffic-log column heading or label for col time, rendered on the dashboard. English: "TIME". */
#define TR_TRAFFIC_COL_TIME "ORA"
/** Italian text for the traffic-log column heading or label for col type, rendered on the dashboard. English: "TYPE". */
#define TR_TRAFFIC_COL_TYPE "TIPO"
/** Italian text for the traffic-log column heading or label for col dx, rendered on the dashboard. English: "DX". */
#define TR_TRAFFIC_COL_DX "DX"
/** Italian text for the traffic-log column heading or label for col packet, rendered on the dashboard. English: "PACKET". */
#define TR_TRAFFIC_COL_PACKET "PACCHETTO"
/** Italian text for the traffic-log column heading or label for col audio, rendered on the dashboard. English: "AUDIO". */
#define TR_TRAFFIC_COL_AUDIO "AUDIO"
/** Italian text for the column header of the IGate traffic table holding the fields decoded out of the packet. English: "DECODED". */
#define TR_TRAFFIC_COL_DECODED "DECODIFICATO"
/** Italian text for the system-information row label for chip, rendered on the dashboard. English: "Chip". */
#define TR_SYSINFO_CHIP "Chip"
/** Italian text for the system-information row label for model, rendered on the dashboard. English: "Model:". */
#define TR_SYSINFO_MODEL "Modello:"
/** Italian text for the system-information row label for cores, rendered on the dashboard. English: "Cores:". */
#define TR_SYSINFO_CORES "Core:"
/** Italian text for the system-information row label for revision, rendered on the dashboard. English: "Revision:". */
#define TR_SYSINFO_REVISION "Revisione:"
/** Italian text for the system-information row label for cpu freq, rendered on the dashboard. English: "CPU speed:". */
#define TR_SYSINFO_CPU_FREQ "Frequenza CPU:"
/** Italian text for the system-information row label for cpu freq set, rendered on the dashboard. English: "Set CPU frequency". */
#define TR_SYSINFO_CPU_FREQ_SET "Imposta frequenza CPU"
/** Italian text for the system-information row label for cpu freq note, rendered on the dashboard. English: "Saved to flash and re-applied automatically on
 * every boot.". */
#define TR_SYSINFO_CPU_FREQ_NOTE "Salvato in flash e riapplicato automaticamente ad ogni avvio."
/** Italian text for the system-information row label for flash size, rendered on the dashboard. English: "Flash size:". */
#define TR_SYSINFO_FLASH_SIZE "Dimensione flash:"
/** Italian text for the system-information row label for min free heap, rendered on the dashboard. English: "Min free heap:". */
#define TR_SYSINFO_MIN_FREE_HEAP "Heap libero minimo:"
/** Italian text for the dashboard label or value for reboot reason, rendered on the dashboard. English: "Reboot Reason:". */
#define TR_DASH_REBOOT_REASON "Motivo riavvio:"

/** @} */

/**
 * @name page_common.c
 * @{
 */
/** Italian text for the dashboard label or value for radio info, rendered on the dashboard. English: "Radio Info". */
#define TR_DASH_RADIO_INFO "Info Radio"
/** Italian text for the dashboard label or value for modem, rendered on the dashboard. English: "MODEM". */
#define TR_DASH_MODEM "MODEM"
/** Italian text for the dashboard label or value for fx25, rendered on the dashboard. English: "FX.25". */
#define TR_DASH_FX25 "FX.25"
/** Italian text for the dashboard label or value for aprs is server, rendered on the dashboard. English: "APRS-IS SERVER". */
#define TR_DASH_APRS_IS_SERVER "SERVER APRS-IS"
/** Italian text for the dashboard label or value for host, rendered on the dashboard. English: "HOST". */
#define TR_DASH_HOST "HOST"
/** Italian text for the dashboard label or value for port, rendered on the dashboard. English: "PORT". */
#define TR_DASH_PORT "PORTA"
/** Italian text for the dashboard label or value for wifi, rendered on the dashboard. English: "WiFi". */
#define TR_DASH_WIFI "WiFi"
/** Italian text for the dashboard label or value for mode, rendered on the dashboard. English: "MODE". */
#define TR_DASH_MODE "MODALITÀ"
/** Italian text for the dashboard label or value for ssid, rendered on the dashboard. English: "SSID". */
#define TR_DASH_SSID "SSID"
/** Italian text for the dashboard label or value for rssi, rendered on the dashboard. English: "RSSI". */
#define TR_DASH_RSSI "RSSI"
/** Italian text for the dashboard label or value for disconnected, rendered on the dashboard. English: "Disconnect". */
#define TR_DASH_DISCONNECTED "Disconnetti"
/** Italian text for the dashboard label or value for modes enabled, rendered on the dashboard. English: "Modes Enabled". */
#define TR_DASH_MODES_ENABLED "Modalità abilitate"
/** Italian text for the dashboard label or value for network status, rendered on the dashboard. English: "Network Status". */
#define TR_DASH_NETWORK_STATUS "Stato rete"
/** Italian text for the dashboard label or value for statistics, rendered on the dashboard. English: "STATISTICS". */
#define TR_DASH_STATISTICS "STATISTICHE"
/** Italian text for the dashboard label or value for radio rx, rendered on the dashboard. English: "RADIO RX:". */
#define TR_DASH_RADIO_RX "RADIO RX:"
/** Italian text for the dashboard label or value for packet tx, rendered on the dashboard. English: "RADIO TX:". */
#define TR_DASH_PACKET_TX "RADIO TX:"
/** Italian text for the dashboard label or value for rf2inet, rendered on the dashboard. English: "RF2INET:". */
#define TR_DASH_RF2INET "RF2INET:"
/** Italian text for the dashboard label or value for inet2rf, rendered on the dashboard. English: "INET2RF:". */
#define TR_DASH_INET2RF "INET2RF:"
/** Italian text for the dashboard label or value for igate rx, rendered on the dashboard. English: "IGATE RX:". */
#define TR_DASH_IGATE_RX "IGATE RX:"
/** Italian text for the dashboard label or value for igate tx, rendered on the dashboard. English: "IGATE TX:". */
#define TR_DASH_IGATE_TX "IGATE TX:"
/** Italian text for the dashboard label or value for digi stat, rendered on the dashboard. English: "DIGI:". */
#define TR_DASH_DIGI_STAT "DIGI:"
/** Italian text for the dashboard label or value for drop err, rendered on the dashboard. English: "DROP/ERR:". */
#define TR_DASH_DROP_ERR "SCARTI/ERR:"
/** Italian text for the dashboard label or value for drop breakdown, rendered on the dashboard. English: "Drop Breakdown". */
#define TR_DASH_DROP_BREAKDOWN "Dettaglio Scarti"
/** Italian text for the dashboard label or value for tx queue, rendered on the dashboard. English: "RF TX QUEUE:". */
#define TR_DASH_TX_QUEUE "CODA TX RF:"
/** Italian text for the dashboard label or value for csma forced, rendered on the dashboard. English: "CSMA FORCED (BUSY/PERSIST):". */
#define TR_DASH_CSMA_FORCED "CSMA FORZATO (OCCUP./PERSIST.):"
/** Italian text for the dashboard label or value for tx duty cycle, rendered on the dashboard. English: "TX DUTY CYCLE:". */
#define TR_DASH_TX_DUTY_CYCLE "CICLO DI LAVORO TX:"
/** Italian text for the dashboard label or value for lh icon, rendered on the dashboard. English: "ICON". */
#define TR_DASH_LH_ICON "ICONA"

/** @} */

/**
 * @name page_digi.c / page_igate.c / page_tracker.c telemetry notes
 * @{
 */
/** Italian text for the explanatory note shown beside the tlm digi setting, rendered on the Digipeater, IGate and Tracker pages. English: "Telemetry
 * (EQNS/PARM/UNIT) for Digi beacons is configured on the <a href='/tlm'>Telemet...". */
#define TR_NOTE_TLM_DIGI                                                                                                                                       \
    "La telemetria (EQNS/PARM/UNIT) per i beacon Digi si configura nella pagina "                                                                              \
    "<a href='/tlm'>Telemetria</a>."
/** Italian text for the explanatory note shown beside the tlm igate setting, rendered on the Digipeater, IGate and Tracker pages. English: "Telemetry
 * (EQNS/PARM/UNIT) for IGate beacons is configured on the <a href='/tlm'>Teleme...". */
#define TR_NOTE_TLM_IGATE                                                                                                                                      \
    "La telemetria (EQNS/PARM/UNIT) per i beacon IGate si configura nella pagina "                                                                             \
    "<a href='/tlm'>Telemetria</a>."
/** Italian text for the explanatory note shown beside the tlm tracker setting, rendered on the Digipeater, IGate and Tracker pages. English: "Telemetry
 * (EQNS/PARM/UNIT) for Tracker beacons is configured on the <a href='/tlm'>Tele...". */
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
/** Italian text for the form label for the "snd rcv msg" field or fieldset, rendered on the Send/Receive Message page. English: "Snd/Rcv Msg". */
#define TR_F_SND_RCV_MSG "Inv/Ric Msg"
/** Italian text for the message page label for my station, rendered on the Send/Receive Message page. English: "My Station:". */
#define TR_MSGCHAT_MY_STATION "La Mia Stazione:"
/** Italian text for the message page label for disabled note, rendered on the Send/Receive Message page. English: "APRS Messaging is disabled or no station
 * callsign is configured. Enable it and set a ca...". */
#define TR_MSGCHAT_DISABLED_NOTE                                                                                                                               \
    "La Messaggistica APRS è disabilitata o non è configurato alcun nominativo. Abilitala e imposta un nominativo nella pagina Message."
/** Italian text for the message page label for loading, rendered on the Send/Receive Message page. English: "Loading messages...". */
#define TR_MSGCHAT_LOADING "Caricamento messaggi..."
/** Italian text for the message page label for empty, rendered on the Send/Receive Message page. English: "No messages yet.". */
#define TR_MSGCHAT_EMPTY "Ancora nessun messaggio."
/** Italian text for the message page label for to, rendered on the Send/Receive Message page. English: "To (callsign):". */
#define TR_MSGCHAT_TO "A (nominativo):"
/** Italian text for the message page label for to placeholder, rendered on the Send/Receive Message page. English: "N0CALL-9". */
#define TR_MSGCHAT_TO_PLACEHOLDER "N0CALL-9"
/** Italian text for the message page label for text, rendered on the Send/Receive Message page. English: "Message:". */
#define TR_MSGCHAT_TEXT "Messaggio:"
/** Italian text for the message page label for text placeholder, rendered on the Send/Receive Message page. English: "Type a message...". */
#define TR_MSGCHAT_TEXT_PLACEHOLDER "Scrivi un messaggio..."
/** Italian text for the message page label for send, rendered on the Send/Receive Message page. English: "Send". */
#define TR_MSGCHAT_SEND "Invia"
/** Italian text for the message page label for you, rendered on the Send/Receive Message page. English: "You". */
#define TR_MSGCHAT_YOU "Tu"
/** Italian text for the message page label for err empty, rendered on the Send/Receive Message page. English: "Enter a destination callsign and a message.". */
#define TR_MSGCHAT_ERR_EMPTY "Inserisci un nominativo di destinazione e un messaggio."
/** Italian text for the message page label for err disabled, rendered on the Send/Receive Message page. English: "APRS Messaging is disabled on the Message
 * page.". */
#define TR_MSGCHAT_ERR_DISABLED "La Messaggistica APRS è disabilitata nella pagina Message."
/** Italian text for the message page label for err no mycall, rendered on the Send/Receive Message page. English: "No station callsign configured.". */
#define TR_MSGCHAT_ERR_NO_MYCALL "Nessun nominativo di stazione configurato."
/** Italian text for the message page label for sent ok, rendered on the Send/Receive Message page. English: "Sent.". */
#define TR_MSGCHAT_SENT_OK "Inviato."
/** Italian text for the message page label for sent fail, rendered on the Send/Receive Message page. English: "Send failed.". */
#define TR_MSGCHAT_SENT_FAIL "Invio non riuscito."

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
/** Italian text for the file Storage page label for usage, rendered on the File Storage page. English: "LittleFS usage:". */
#define TR_STORAGE_USAGE "Utilizzo LittleFS:"
/** Italian text for the file Storage page label for upload file, rendered on the File Storage page. English: "Upload file". */
#define TR_STORAGE_UPLOAD_FILE "Carica file"
/** Italian text for the file Storage page label for confirm format, rendered on the File Storage page. English: "Erase ALL files and reset config to
 * defaults?". */
#define TR_STORAGE_CONFIRM_FORMAT "Cancellare TUTTI i file e ripristinare la configurazione predefinita?"
/** Italian text for the file Storage page label for format btn, rendered on the File Storage page. English: "Format LittleFS". */
#define TR_STORAGE_FORMAT_BTN "Formatta LittleFS"
/** Italian text for the file Storage page label for size bytes, rendered on the File Storage page. English: "Size (bytes)". */
#define TR_STORAGE_SIZE_BYTES "Dimensione (byte)"
/** Italian text for the file Storage page label for actions, rendered on the File Storage page. English: "Actions". */
#define TR_STORAGE_ACTIONS "Azioni"
/** Italian text for the file Storage page label for download, rendered on the File Storage page. English: "Download". */
#define TR_STORAGE_DOWNLOAD "Scarica"
/** Italian text for the file Storage page label for confirm delete prefix, rendered on the File Storage page. English: "Delete". */
#define TR_STORAGE_CONFIRM_DELETE_PREFIX "Elimina "
/** Italian text for the file Storage page label for delete, rendered on the File Storage page. English: "Delete". */
#define TR_STORAGE_DELETE "Elimina"
/** Italian text for the file Storage page label for upload ok, rendered on the File Storage page. English: "Uploaded:". */
#define TR_STORAGE_UPLOAD_OK "Caricato:"
/** Italian text for the file Storage page label for upload failed, rendered on the File Storage page. English: "Upload failed. Check the file and make sure
 * there is enough free space.". */
#define TR_STORAGE_UPLOAD_FAILED "Caricamento non riuscito. Controlla il file e lo spazio libero disponibile."
/** Italian text for the file Storage page label for no file chosen, rendered on the File Storage page. English: "Choose a file first.". */
#define TR_STORAGE_NO_FILE_CHOSEN "Seleziona prima un file."
/** Italian text for the file Storage page label for back, rendered on the File Storage page. English: "Back". */
#define TR_STORAGE_BACK "Indietro"

/** @} */

/**
 * @name page_symbol.c
 * @{
 */
/** Italian text for the symbol-picker label for house hf, rendered on the symbol picker. English: "House (HF)". */
#define TR_SYM_HOUSE_HF "Casa (HF)"
/** Italian text for the symbol-picker label for car, rendered on the symbol picker. English: "Car". */
#define TR_SYM_CAR "Auto"
/** Italian text for the symbol-picker label for motorcycle, rendered on the symbol picker. English: "Motorcycle". */
#define TR_SYM_MOTORCYCLE "Moto"
/** Italian text for the symbol-picker label for bicycle, rendered on the symbol picker. English: "Bicycle". */
#define TR_SYM_BICYCLE "Bicicletta"
/** Italian text for the symbol-picker label for truck, rendered on the symbol picker. English: "Truck". */
#define TR_SYM_TRUCK "Camion"
/** Italian text for the symbol-picker label for van, rendered on the symbol picker. English: "Van". */
#define TR_SYM_VAN "Furgone"
/** Italian text for the symbol-picker label for jeep, rendered on the symbol picker. English: "Jeep". */
#define TR_SYM_JEEP "Jeep"
/** Italian text for the symbol-picker label for fire truck, rendered on the symbol picker. English: "Fire truck". */
#define TR_SYM_FIRE_TRUCK "Autopompa"
/** Italian text for the symbol-picker label for police, rendered on the symbol picker. English: "Police". */
#define TR_SYM_POLICE "Polizia"
/** Italian text for the symbol-picker label for house, rendered on the symbol picker. English: "House". */
#define TR_SYM_HOUSE "Casa"
/** Italian text for the symbol-picker label for digipeater, rendered on the symbol picker. English: "Digipeater". */
#define TR_SYM_DIGIPEATER "Digipeater"
/** Italian text for the symbol-picker label for gateway, rendered on the symbol picker. English: "Gateway". */
#define TR_SYM_GATEWAY "Gateway"
/** Italian text for the symbol-picker label for weather station, rendered on the symbol picker. English: "Weather station". */
#define TR_SYM_WEATHER_STATION "Stazione meteo"
/** Italian text for the symbol-picker label for balloon, rendered on the symbol picker. English: "Balloon". */
#define TR_SYM_BALLOON "Pallone aerostatico"
/** Italian text for the symbol-picker label for space shuttle, rendered on the symbol picker. English: "Space shuttle". */
#define TR_SYM_SPACE_SHUTTLE "Navetta spaziale"
/** Italian text for the symbol-picker label for sailboat, rendered on the symbol picker. English: "Sailboat". */
#define TR_SYM_SAILBOAT "Barca a vela"
/** Italian text for the symbol-picker label for nws site, rendered on the symbol picker. English: "NWS site". */
#define TR_SYM_NWS_SITE "Sito NWS"
/** Italian text for the symbol-picker label for tcp ip, rendered on the symbol picker. English: "TCP/IP". */
#define TR_SYM_TCP_IP "TCP/IP"
/** Italian text for the symbol-picker label for car alt, rendered on the symbol picker. English: "Car (alternate table)". */
#define TR_SYM_CAR_ALT "Auto (tabella alternativa)"
/** Italian text for the symbol-picker label for wx station alt, rendered on the symbol picker. English: "WX station (alternate table)". */
#define TR_SYM_WX_STATION_ALT "Stazione WX (tabella alternativa)"
/** Italian text for the symbol-picker label for intro, rendered on the symbol picker. English: "Quick reference for common APRS symbol codes. Each service page
 * (IGate / Digi / Tracker...". */
#define TR_SYM_INTRO                                                                                                                                           \
    "Riferimento rapido per i codici simbolo APRS più comuni. Ogni pagina di servizio "                                                                        \
    "(IGate / Digi / Tracker) ha il proprio campo simbolo a testo libero \u2014 copia il "                                                                     \
    "codice a 2 caratteri da qui in quel campo."
/** Italian text for the symbol-picker label for code, rendered on the symbol picker. English: "Code". */
#define TR_SYM_CODE "Codice"
/** Italian text for the symbol-picker label for meaning, rendered on the symbol picker. English: "Meaning". */
#define TR_SYM_MEANING "Significato"
/** Italian text for the symbol-picker label for currently configured, rendered on the symbol picker. English: "Currently configured symbols". */
#define TR_SYM_CURRENTLY_CONFIGURED "Simboli attualmente configurati"

/** @} */

/**
 * @name page_system.c
 * @{
 */
/** Italian text for the system page label for web admin login, rendered on the System page. English: "Web Admin Login". */
#define TR_SYS_WEB_ADMIN_LOGIN "Accesso amministrazione web"
/** Italian text for the system page label for time, rendered on the System page. English: "Time". */
#define TR_SYS_TIME "Ora"
/** Italian text for the system page label for sync ntp, rendered on the System page. English: "Sync time via NTP". */
#define TR_SYS_SYNC_NTP "Sincronizza ora via NTP"
/** Italian text for the system page label for ntp host, rendered on the System page. English: "NTP host (primary)". */
#define TR_SYS_NTP_HOST "Host NTP (primario)"
/** Italian text for the system page label for ntp host2, rendered on the System page. English: "NTP host (fallback 2)". */
#define TR_SYS_NTP_HOST2 "Host NTP (riserva 2)"
/** Italian text for the system page label for ntp host3, rendered on the System page. English: "NTP host (fallback 3)". */
#define TR_SYS_NTP_HOST3 "Host NTP (riserva 3)"
/** Italian text for the system page label for ntp resync, rendered on the System page. English: "NTP resync interval (s, min 30)". */
#define TR_SYS_NTP_RESYNC "Intervallo risincronizzazione NTP (s, min 30)"
/** Italian text for the system page label for timezone, rendered on the System page. English: "Time zone (dashboard display only)". */
#define TR_SYS_TIMEZONE "Fuso orario (solo per la dashboard)"
/** Italian text for the system page label for digi path aliases, rendered on the System page. English: "Digipeater Path Aliases". */
#define TR_SYS_DIGI_PATH_ALIASES "Alias percorso Digipeater"
/** Italian text for the system page label for path 1, rendered on the System page. English: "Path 1". */
#define TR_SYS_PATH_1 "Percorso 1"
/** Italian text for the system page label for path 2, rendered on the System page. English: "Path 2". */
#define TR_SYS_PATH_2 "Percorso 2"
/** Italian text for the system page label for path 3, rendered on the System page. English: "Path 3". */
#define TR_SYS_PATH_3 "Percorso 3"
/** Italian text for the system page label for path 4, rendered on the System page. English: "Path 4". */
#define TR_SYS_PATH_4 "Percorso 4"
/** Italian text for the system page label for confirm factory reset, rendered on the System page. English: "Reset ALL settings to factory defaults?". */
#define TR_SYS_CONFIRM_FACTORY_RESET "Ripristinare TUTTE le impostazioni ai valori di fabbrica?"
/** Italian text for the system page label for factory reset, rendered on the System page. English: "Factory Reset". */
#define TR_SYS_FACTORY_RESET "Ripristino di fabbrica"

/** @} */

/**
 * @name page_tlm.c
 * @{
 */
/** Italian text for the telemetry configurator label for avg, rendered on the Telemetry page. English: "Avg". */
#define TR_TLM_AVG "Media"
/** Italian text for the telemetry configurator label for bit, rendered on the Telemetry page. English: "Bit". */
#define TR_TLM_BIT "Bit"
/** @} */

/**
 * @name page_tlm.c: telemetry configurator
 * @{
 */
/** Italian text for the telemetry configurator label for enable telemetry, rendered on the Telemetry page. English: "Enable Telemetry". */
#define TR_TLM_ENABLE_TELEMETRY "Abilita telemetria"
/** Italian text for the telemetry configurator label for report params, rendered on the Telemetry page. English: "Report Parameters". */
#define TR_TLM_REPORT_PARAMS "Parametri del report"
/** Italian text for the telemetry configurator label for path digis, rendered on the Telemetry page. English: "Path (digipeaters)". */
#define TR_TLM_PATH_DIGIS "Percorso (digipeater)"
/** Italian text for the telemetry configurator label for destination, rendered on the Telemetry page. English: "Destination". */
#define TR_TLM_DESTINATION "Destinazione"
/** Italian text for the telemetry configurator label for auto inc seq, rendered on the Telemetry page. English: "Auto-increment sequence". */
#define TR_TLM_AUTO_INC_SEQ "Incremento automatico sequenza"
/** Italian text for the telemetry configurator label for analog field width, rendered on the Telemetry page. English: "Analog field width". */
#define TR_TLM_ANALOG_FIELD_WIDTH "Larghezza campo analogico"
/** Italian text for the telemetry configurator label for fieldw 3digit, rendered on the Telemetry page. English: "3-digit zero-padded (000-255, strict)". */
#define TR_TLM_FIELDW_3DIGIT "3 cifre con zeri (000-255, rigoroso)"
/** Italian text for the telemetry configurator label for fieldw auto, rendered on the Telemetry page. English: "Minimal / as-needed (integers or decimals)". */
#define TR_TLM_FIELDW_AUTO "Minimo / secondo necessità (interi o decimali)"
/** Italian text for the telemetry configurator label for omit trailing, rendered on the Telemetry page. English: "Omit unused trailing channels". */
#define TR_TLM_OMIT_TRAILING "Ometti canali finali inutilizzati"
/** Italian text for the telemetry configurator label for trail comment, rendered on the Telemetry page. English: "Trailing comment (optional, after bits)". */
#define TR_TLM_TRAIL_COMMENT "Commento finale (opzionale, dopo i bit)"
/** Italian text for the telemetry configurator label for comment tlm, rendered on the Telemetry page. English: "Also carry telemetry in position comment
 * (APRS 1.2, |ss..|)". */
#define TR_TLM_COMMENT_TLM "Includi anche la telemetria nel commento di posizione (APRS 1.2, |ss..|)"
/** Italian text for the telemetry configurator label for analog count, rendered on the Telemetry page. English: "Analog channels sent". */
#define TR_TLM_ANALOG_COUNT "Canali analogici inviati"
/** Italian text for the telemetry configurator label for digital count, rendered on the Telemetry page. English: "Digital bits sent". */
#define TR_TLM_DIGITAL_COUNT "Bit digitali inviati"
/** Italian text for the telemetry configurator label for def messages, rendered on the Telemetry page. English: "Definition Messages". */
#define TR_TLM_DEF_MESSAGES "Messaggi di definizione"
/** Italian text for the telemetry configurator label for gen parm, rendered on the Telemetry page. English: "PARM - channel & bit names". */
#define TR_TLM_GEN_PARM "PARM - nomi canale e bit"
/** Italian text for the telemetry configurator label for gen unit, rendered on the Telemetry page. English: "UNIT - units / bit-state labels". */
#define TR_TLM_GEN_UNIT "UNIT - unità / etichette stato bit"
/** Italian text for the telemetry configurator label for gen eqns, rendered on the Telemetry page. English: "EQNS - scaling coefficients (A,B,C)". */
#define TR_TLM_GEN_EQNS "EQNS - coefficienti di scala (A,B,C)"
/** Italian text for the telemetry configurator label for gen bits, rendered on the Telemetry page. English: "BITS - bit sense + name". */
#define TR_TLM_GEN_BITS "BITS - senso del bit + nome"
/** Italian text for the telemetry configurator label for analog legend, rendered on the Telemetry page. English: "Analog Channels (A1-A5)". */
#define TR_TLM_ANALOG_LEGEND "Canali analogici (A1-A5)"
/** Italian text for the telemetry configurator label for digital legend, rendered on the Telemetry page. English: "Digital Channels (B1-B8)". */
#define TR_TLM_DIGITAL_LEGEND "Canali digitali (B1-B8)"
/** Italian text for the telemetry configurator label for unit, rendered on the Telemetry page. English: "Unit". */
#define TR_TLM_UNIT "Unità"
/** Italian text for the telemetry configurator label for source, rendered on the Telemetry page. English: "Source". */
#define TR_TLM_SOURCE "Sorgente"
/** Italian text for the telemetry configurator label for rf, rendered on the Telemetry page. English: "RF". */
#define TR_TLM_RF "RF"
/** Italian text for the telemetry configurator label for raw min, rendered on the Telemetry page. English: "Raw min". */
#define TR_TLM_RAW_MIN "Grezzo min"
/** Italian text for the telemetry configurator label for raw max, rendered on the Telemetry page. English: "Raw max". */
#define TR_TLM_RAW_MAX "Grezzo max"
/** Italian text for the telemetry configurator label for coef a, rendered on the Telemetry page. English: "A (quadratic)". */
#define TR_TLM_COEF_A "A (quadratico)"
/** Italian text for the telemetry configurator label for coef b, rendered on the Telemetry page. English: "B (linear / slope)". */
#define TR_TLM_COEF_B "B (lineare / pendenza)"
/** Italian text for the telemetry configurator label for coef c, rendered on the Telemetry page. English: "C (offset)". */
#define TR_TLM_COEF_C "C (scostamento)"
/** Italian text for the telemetry configurator label for decimals, rendered on the Telemetry page. English: "Displayed decimals". */
#define TR_TLM_DECIMALS "Decimali mostrati"
/** Italian text for the telemetry configurator label for on state, rendered on the Telemetry page. English: "On-state means". */
#define TR_TLM_ON_STATE "Significato stato attivo"
/** Italian text for the telemetry configurator label for sense, rendered on the Telemetry page. English: "Sense". */
#define TR_TLM_SENSE "Senso"
/** Italian text for the telemetry configurator label for label, rendered on the Telemetry page. English: "Label". */
#define TR_TLM_LABEL "Etichetta"
/** Italian text for the telemetry configurator label for calib wizard, rendered on the Telemetry page. English: "2-point calibration wizard". */
#define TR_TLM_CALIB_WIZARD "Procedura di calibrazione a 2 punti"
/** Italian text for the telemetry configurator label for calib prompt x1, rendered on the Telemetry page. English: "Raw reading #1 (x1):". */
#define TR_TLM_CALIB_PROMPT_X1 "Lettura grezza #1 (x1):"
/** Italian text for the telemetry configurator label for calib prompt y1, rendered on the Telemetry page. English: "Known real-world value at x1:". */
#define TR_TLM_CALIB_PROMPT_Y1 "Valore reale noto a x1:"
/** Italian text for the telemetry configurator label for calib prompt x2, rendered on the Telemetry page. English: "Raw reading #2 (x2):". */
#define TR_TLM_CALIB_PROMPT_X2 "Lettura grezza #2 (x2):"
/** Italian text for the telemetry configurator label for calib prompt y2, rendered on the Telemetry page. English: "Known real-world value at x2:". */
#define TR_TLM_CALIB_PROMPT_Y2 "Valore reale noto a x2:"
/** Italian text for the telemetry configurator label for calib same x, rendered on the Telemetry page. English: "x1 and x2 must differ.". */
#define TR_TLM_CALIB_SAME_X "x1 e x2 devono essere diversi."
/** Italian text for the telemetry configurator label for calib cancelled, rendered on the Telemetry page. English: "Calibration cancelled: enter numeric
 * values.". */
#define TR_TLM_CALIB_CANCELLED "Calibrazione annullata: inserire valori numerici."

/** @} */

/**
 * @name page_radio.c
 * @{
 */
/** Italian text for the radiomodem page label for audio hw title, rendered on the Radiomodem page. English: "Audio hardware (compile-time)". */
#define TR_RADIO_AUDIO_HW_TITLE "Hardware audio (in fase di compilazione)"
/** Italian text for the radiomodem page label for audio hw info, rendered on the Radiomodem page. English: "<br>DAC out: GPIO%d<br>ADC in: GPIO%d<br>PTT pin:
 * %s<br>PTT active-high: %s<br>ADC atte...". */
#define TR_RADIO_AUDIO_HW_INFO                                                                                                                                 \
    "<br>DAC uscita: GPIO%d<br>ADC ingresso: GPIO%d<br>Pin PTT: %s<br>PTT attivo-alto: %s<br>Attenuazione ADC: %d<br>ADC: %d Hz<br>DAC: %d Hz"
/** Italian text: Radiomodem page label for audio hw note, rendered on the Radiomodem page. */
#define TR_RADIO_AUDIO_HW_NOTE ""

/** @} */

/**
 * @name page_wireless.c
 * @{
 */
/** Italian text for the wireless page label for mode legend, rendered on the Wireless page. English: "WiFi Mode". */
#define TR_WIFI_MODE_LEGEND "Modalità WiFi"
/** Italian text for the wireless page label for station, rendered on the Wireless page. English: "Station (STA)". */
#define TR_WIFI_STATION "Stazione (STA)"
/** Italian text for the wireless page label for access point, rendered on the Wireless page. English: "Access Point (AP)". */
#define TR_WIFI_ACCESS_POINT "Access Point (AP)"
/** Italian text for the wireless page label for ap sta, rendered on the Wireless page. English: "AP + STA". */
#define TR_WIFI_AP_STA "AP + STA"
/** Italian text for the wireless page label for tx power, rendered on the Wireless page. English: "TX Power (0-20 dBm)". */
#define TR_WIFI_TX_POWER "Potenza TX (0-20 dBm)"
/** Italian text for the wireless page label for ap ssid, rendered on the Wireless page. English: "AP SSID". */
#define TR_WIFI_AP_SSID "SSID AP"
/** Italian text for the wireless page label for ap password, rendered on the Wireless page. English: "AP Password". */
#define TR_WIFI_AP_PASSWORD "Password AP"
/** Italian text for the wireless page label for ap channel, rendered on the Wireless page. English: "AP Channel". */
#define TR_WIFI_AP_CHANNEL "Canale AP"
/** Italian text for the wireless page label for client legend, rendered on the Wireless page. English: "WiFi Client #%d". */
#define TR_WIFI_CLIENT_LEGEND "Client WiFi #%d"
/** Italian text for the caption of the "wifi scan" button, rendered on the Wireless page. English: "WIFI SCAN". */
#define TR_BTN_WIFI_SCAN "SCANSIONA WIFI"
/** Italian text for the wireless page label for ssid placeholder, rendered on the Wireless page. English: "Network name (type it, or use WiFi Scan)". */
#define TR_WIFI_SSID_PLACEHOLDER "Nome rete (digitalo, oppure usa Scansiona WiFi)"
/** Italian text for the wireless page label for sta needs ssid, rendered on the Wireless page. English: "Saved, but this will NOT connect: Mode selects a
 * station, yet no WiFi Client block has...". */
#define TR_WIFI_STA_NEEDS_SSID                                                                                                                                 \
    "Salvato, ma questo NON si connetterà: la modalità seleziona una stazione, ma nessun blocco Client WiFi ha sia 'Abilita' selezionato sia un SSID "         \
    "compilato. Correggi e salva di nuovo."
/** Italian text for the wireless page label for scanning, rendered on the Wireless page. English: "Scanning...". */
#define TR_WIFI_SCANNING "Scansione in corso..."
/** Italian text for the wireless page label for scan failed, rendered on the Wireless page. English: "Scan failed". */
#define TR_WIFI_SCAN_FAILED "Scansione non riuscita"

/** @} */

/**
 * @name page_wx.c
 * @{
 */
/** Italian text for the weather page label for wind speed, rendered on the Weather page. English: "Wind Speed". */
#define TR_WX_WIND_SPEED "Velocità vento"
/** Italian text for the weather page label for wind gust, rendered on the Weather page. English: "Wind Gust". */
#define TR_WX_WIND_GUST "Raffica vento"
/** Italian text for the weather page label for wind direction, rendered on the Weather page. English: "Wind Direction". */
#define TR_WX_WIND_DIRECTION "Direzione vento"
/** Italian text for the weather page label for temperature, rendered on the Weather page. English: "Temperature". */
#define TR_WX_TEMPERATURE "Temperatura"
/** Italian text for the weather page label for rain 1h, rendered on the Weather page. English: "Rain 1h". */
#define TR_WX_RAIN_1H "Pioggia 1h"
/** Italian text for the weather page label for rain 24h, rendered on the Weather page. English: "Rain 24h". */
#define TR_WX_RAIN_24H "Pioggia 24h"
/** Italian text for the weather page label for rain midnight, rendered on the Weather page. English: "Rain since midnight". */
#define TR_WX_RAIN_MIDNIGHT "Pioggia da mezzanotte"
/** Italian text for the weather page label for humidity, rendered on the Weather page. English: "Humidity". */
#define TR_WX_HUMIDITY "Umidità"
/** Italian text for the weather page label for pressure, rendered on the Weather page. English: "Pressure". */
#define TR_WX_PRESSURE "Pressione"
/** Italian text for the weather page label for luminosity, rendered on the Weather page. English: "Luminosity". */
#define TR_WX_LUMINOSITY "Luminosità"
/** Italian text for the weather page label for snow, rendered on the Weather page. English: "Snow". */
#define TR_WX_SNOW "Neve"
/** Italian text for the weather page label for flood ft, rendered on the Weather page. English: "Flood Height (ft)". */
#define TR_WX_FLOOD_FT "Livello piena (ft)"
/** Italian text for the weather page label for flood m, rendered on the Weather page. English: "Flood Height". */
#define TR_WX_FLOOD_M "Livello piena"
/** Weather page label for the raw rain counter, rendered on the Weather page. */
#define TR_WX_RAIN_RAW "Contatore pioggia grezzo"
/** Italian text for the weather page label for field, rendered on the Weather page. English: "WX Field". */
#define TR_WX_FIELD "Campo WX"
/** Italian text for the weather page label for channel, rendered on the Weather page. English: "Channel". */
#define TR_WX_CHANNEL "Canale"
/** Italian text for the weather page label for channel none, rendered on the Weather page. English: "(none)". */
#define TR_WX_CHANNEL_NONE "(nessuno)"
/** Italian text for the weather page label for value, rendered on the Weather page. English: "Value". */
#define TR_WX_VALUE "Valore"

/** @} */

/**
 * @name IGATE page additions (station symbol, path preset, timestamp, PHG, filters)
 * @{
 */
/** Italian text for the form label for the "station symbol" field or fieldset, rendered on the IGate page. English: "Station Symbol". */
#define TR_F_STATION_SYMBOL "Simbolo stazione"
/** Italian text for the form label for the "symbol table" field or fieldset, rendered on the IGate page. English: "Table". */
#define TR_F_SYMBOL_TABLE "Tabella"
/** Italian text for the form label for the "symbol code" field or fieldset, rendered on the IGate page. English: "Symbol". */
#define TR_F_SYMBOL_CODE "Simbolo"
/** Italian text for the caption of the "pick symbol" button, rendered on the IGate page. English: "...". */
#define TR_BTN_PICK_SYMBOL "..."
/** Italian text for the symbol-picker label for pick hint, rendered on the IGate page. English: "Click icon for select symbol". */
#define TR_SYM_PICK_HINT "Clicca sull'icona per selezionare il simbolo"
/** Italian text for the form label for the "path" field or fieldset, rendered on the IGate page. English: "PATH". */
#define TR_F_PATH "PERCORSO"
/** Italian text for the digipeat path preset label for direct, rendered on the IGate page. English: "Direct (no path)". */
#define TR_PATH_DIRECT "Diretto (nessun percorso)"
/** Italian text for the digipeat path preset label for custom unset, rendered on the IGate page. English: "(not set)". */
#define TR_PATH_CUSTOM_UNSET "(non impostato)"
/** Italian text for the digipeat path preset label for direct hint, rendered on the IGate page. English: "no digipeater path - only stations that hear you
 * directly will receive it". */
#define TR_PATH_DIRECT_HINT "nessun percorso digipeater - solo le stazioni che ti ricevono direttamente lo riceveranno"
/** Italian text for the digipeat path preset label for hop hint, rendered on the IGate page. English: "hop(s) via digipeater, encoded as an SSID suffix (short
 * WIDEn-N form)". */
#define TR_PATH_HOP_HINT "hop via digipeater, codificato come suffisso SSID (forma breve WIDEn-N)"
/** Italian text for the digipeat path preset label for custom hint, rendered on the IGate page. English: "custom digipeater path configured on the System
 * page". */
#define TR_PATH_CUSTOM_HINT "percorso digipeater personalizzato configurato nella pagina Sistema"
/** Italian text for the form label for the "time stamp" field or fieldset, rendered on the IGate page. English: "Time Stamp". */
#define TR_F_TIME_STAMP "Timestamp"
/** Italian text for the form label for the "tx channel" field or fieldset, rendered on the IGate page. English: "TX Channel". */
#define TR_F_TX_CHANNEL "Canale TX"
/** Italian text for the form label for the "phg section" field or fieldset, rendered on the IGate page. English: "PHG". */
#define TR_F_PHG_SECTION "PHG"
/** Italian text for the form label for the "enable phg" field or fieldset, rendered on the IGate page. English: "Enable PHG". */
#define TR_F_ENABLE_PHG "Abilita PHG"
/** Italian text for the form label for the "radio tx power" field or fieldset, rendered on the IGate page. English: "Radio TX Power". */
#define TR_F_RADIO_TX_POWER "Potenza TX radio"
/** Italian text for the form label for the "antenna gain" field or fieldset, rendered on the IGate page. English: "Antenna Gain". */
#define TR_F_ANTENNA_GAIN "Guadagno antenna"
/** Italian text for the form label for the "height m" field or fieldset, rendered on the IGate page. English: "Height (m)". */
#define TR_F_HEIGHT_M "Altezza (m)"
/** Italian text for the form label for the "antenna direction" field or fieldset, rendered on the IGate page. English: "Antenna/Direction". */
#define TR_F_ANTENNA_DIRECTION "Antenna/Direzione"
/** Italian text for the form label for the "phg text" field or fieldset, rendered on the IGate page. English: "PHG Text". */
#define TR_F_PHG_TEXT "Testo PHG"
/** Italian text for the form label for the "ext section" field or fieldset, rendered on the IGate page. English: "Data Extension". */
#define TR_F_EXT_SECTION "Estensione dati"
/** Italian text for the form label for the "enable ext" field or fieldset, rendered on the IGate page. English: "Enable data extension". */
#define TR_F_ENABLE_EXT "Abilita estensione dati"
/** Italian text for the form label for the "ext type" field or fieldset, rendered on the IGate page. English: "Extension type". */
#define TR_F_EXT_TYPE "Tipo di estensione"
/** Italian text for the data-extension label for phg, rendered on the IGate page. English: "PHG - power/height/gain/directivity". */
#define TR_EXT_PHG "PHG - potenza/altezza/guadagno/direttivita"
/** Italian text for the data-extension label for rng, rendered on the IGate page. English: "RNG - pre-calculated radio range". */
#define TR_EXT_RNG "RNG - portata radio precalcolata"
/** Italian text for the data-extension label for dfs, rendered on the IGate page. English: "DFS - omni-DF signal strength". */
#define TR_EXT_DFS "DFS - intensita del segnale omni-DF"
/** Italian text for the form label for the "ext range mi" field or fieldset, rendered on the IGate page. English: "Radio range (miles)". */
#define TR_F_EXT_RANGE_MI "Portata radio (miglia)"
/** Italian text for the form label for the "ext dfs strength" field or fieldset, rendered on the IGate page. English: "Signal strength (S-points, 0 = not
 * heard)". */
#define TR_F_EXT_DFS_STRENGTH "Intensita del segnale (punti S, 0 = non ricevuto)"
/** Italian text for the form label for the "pos ambiguity" field or fieldset, rendered on the IGate page. English: "Position ambiguity". */
#define TR_F_POS_AMBIGUITY "Ambiguita di posizione"
/** Italian text for the position-ambiguity selector entry: none, rendered on the IGate page. English: "Full precision". */
#define TR_AMB_NONE "Precisione piena"
/** Italian text for the position-ambiguity selector entry: tenth, rendered on the IGate page. English: "Nearest 1/10 minute". */
#define TR_AMB_TENTH "Al 1/10 di minuto"
/** Italian text for the position-ambiguity selector entry: minute, rendered on the IGate page. English: "Nearest minute". */
#define TR_AMB_MINUTE "Al minuto"
/** Italian text for the position-ambiguity selector entry: ten minutes, rendered on the IGate page. English: "Nearest 10 minutes". */
#define TR_AMB_TEN_MINUTES "Ai 10 minuti"
/** Italian text for the position-ambiguity selector entry: degree, rendered on the IGate page. English: "Nearest degree". */
#define TR_AMB_DEGREE "Al grado"
/** Italian text for the form label for the "status grid" field or fieldset, rendered on the IGate page. English: "Maidenhead locator in status reports". */
#define TR_F_STATUS_GRID "Localizzatore Maidenhead nei rapporti di stato"
/** Italian text for the form label for the "status timestamp" field or fieldset, rendered on the IGate page. English: "Zulu timestamp in status reports". */
#define TR_F_STATUS_TIMESTAMP "Timestamp zulu nei rapporti di stato"
/** Form label for the "status beam" field or fieldset, rendered on the IGate page. */
#define TR_F_STATUS_BEAM "Direzione antenna nei rapporti di stato (gradi)"
/** Form label for the "status erp" field or fieldset, rendered on the IGate page. */
#define TR_F_STATUS_ERP "ERP nei rapporti di stato (W)"
/** Italian text for the form label for the "pos dao" field or fieldset, rendered on the IGate page. English: "DAO precision extension in position reports". */
#define TR_F_POS_DAO "Estensione di precisione DAO nei rapporti di posizione"
/** Italian text for the form label for the "no archive" field or fieldset, rendered on the Station page. English: "Request APRS-IS not to archive my packets
 * (!x!)". */
#define TR_F_NO_ARCHIVE "Richiedi ad APRS-IS di non archiviare i miei pacchetti (!x!)"
/** Italian text for the compass direction abbreviation: omni, rendered on the IGate page. English: "Omni". */
#define TR_DIR_OMNI "Omni"
/** Italian text for the compass direction abbreviation: n, rendered on the IGate page. English: "N". */
#define TR_DIR_N "N"
/** Italian text for the compass direction abbreviation: ne, rendered on the IGate page. English: "NE". */
#define TR_DIR_NE "NE"
/** Italian text for the compass direction abbreviation: e, rendered on the IGate page. English: "E". */
#define TR_DIR_E "E"
/** Italian text for the compass direction abbreviation: se, rendered on the IGate page. English: "SE". */
#define TR_DIR_SE "SE"
/** Italian text for the compass direction abbreviation: s, rendered on the IGate page. English: "S". */
#define TR_DIR_S "S"
/** Italian text for the compass direction abbreviation: sw, rendered on the IGate page. English: "SW". */
#define TR_DIR_SW "SO"
/** Italian text for the compass direction abbreviation: w, rendered on the IGate page. English: "W". */
#define TR_DIR_W "O"
/** Italian text for the compass direction abbreviation: nw, rendered on the IGate page. English: "NW". */
#define TR_DIR_NW "NO"
/** Italian text for the form label for the "igate filter" field or fieldset, rendered on the IGate page. English: "IGate Filter". */
#define TR_F_IGATE_FILTER "Filtro IGate"
/** Italian text for the form label for the "filter rf2inet" field or fieldset, rendered on the IGate page. English: "Filter RF to Internet". */
#define TR_F_FILTER_RF2INET "Filtro RF verso Internet"
/** Italian text for the form label for the "filter inet2rf" field or fieldset, rendered on the IGate page. English: "Filter Internet to RF". */
#define TR_F_FILTER_INET2RF "Filtro Internet verso RF"
/** Italian text for the aPRS-IS filter editor label for message, rendered on the IGate page. English: "Message". */
#define TR_FILT_MESSAGE "Messaggio"
/** Italian text for the aPRS-IS filter editor label for status, rendered on the IGate page. English: "Status". */
#define TR_FILT_STATUS "Stato"
/** Italian text for the aPRS-IS filter editor label for telemetry, rendered on the IGate page. English: "Telemetry". */
#define TR_FILT_TELEMETRY "Telemetria"
/** Italian text for the aPRS-IS filter editor label for weather, rendered on the IGate page. English: "Weather". */
#define TR_FILT_WEATHER "Meteo"
/** Italian text for the aPRS-IS filter editor label for object, rendered on the IGate page. English: "Object". */
#define TR_FILT_OBJECT "Oggetto"
/** Italian text for the aPRS-IS filter editor label for item, rendered on the IGate page. English: "Item". */
#define TR_FILT_ITEM "Item"
/** Italian text for the aPRS-IS filter editor label for buoy, rendered on the IGate page. English: "Buoy". */
#define TR_FILT_BUOY "Boa"
/** Italian text for the aPRS-IS filter editor label for position, rendered on the IGate page. English: "Position". */
#define TR_FILT_POSITION "Posizione"
/** Italian text for the APRS-IS filter editor label for the payload kinds that share one bit (capacità, formati definiti dall'utente, radiogoniometria,
 * radiofari di locatore, elemento di mappa), rendered on the IGate page. English: "Other". */
#define TR_FILT_OTHER "Altri"

/** Italian text for the form label for the "callsign filter" field or fieldset, rendered on the IGate page. English: "Callsign Filter". */
#define TR_F_CALLSIGN_FILTER "Filtro Nominativi"
/** Italian text for the form label for the "budlist mode rf2inet" field or fieldset, rendered on the IGate page. English: "RF to Internet Mode". */
#define TR_F_BUDLIST_MODE_RF2INET "Modalità RF verso Internet"
/** Italian text for the form label for the "budlist mode inet2rf" field or fieldset, rendered on the IGate page. English: "Internet to RF Mode". */
#define TR_F_BUDLIST_MODE_INET2RF "Modalità Internet verso RF"
/** Italian text for the buddy-list editor label for off, rendered on the IGate page. English: "Off". */
#define TR_BUDLIST_OFF "Disattivato"
/** Italian text for the buddy-list editor label for whitelist, rendered on the IGate page. English: "Whitelist". */
#define TR_BUDLIST_WHITELIST "Lista Bianca"
/** Italian text for the buddy-list editor label for blacklist, rendered on the IGate page. English: "Blacklist". */
#define TR_BUDLIST_BLACKLIST "Lista Nera"
/** Italian text for the form label for the "budlist call" field or fieldset, rendered on the IGate page. English: "Callsign". */
#define TR_F_BUDLIST_CALL "Nominativo"
/** Italian text for the explanatory note shown beside the budlist setting, rendered on the IGate page. English: "Shared callsign list, up to 8 entries.
 * Whitelist: only listed calls pass. Blacklist: li...". */
#define TR_NOTE_BUDLIST                                                                                                                                        \
    "Elenco nominativi condiviso, fino a 8 voci. Lista Bianca: passano solo i nominativi elencati. Lista Nera: i nominativi elencati vengono bloccati."

/** Italian text for the form label for the "range filter en" field or fieldset, rendered on the IGate page. English: "Enable range filter". */
#define TR_F_RANGE_FILTER_EN "Abilita filtro di distanza"
/** Italian text for the form label for the "range km" field or fieldset, rendered on the IGate page. English: "Max distance (km, 0 = unlimited)". */
#define TR_F_RANGE_KM "Distanza massima (km, 0 = illimitata)"
/** Italian text for the form label for the "prefix filter en" field or fieldset, rendered on the IGate page. English: "Enable callsign-prefix filter". */
#define TR_F_PREFIX_FILTER_EN "Abilita filtro prefisso nominativo"
/** Italian text for the form label for the "prefixes" field or fieldset, rendered on the IGate page. English: "Allowed prefixes (comma-separated)". */
#define TR_F_PREFIXES "Prefissi consentiti (separati da virgola)"
/** Italian text for the explanatory note shown beside the range prefix setting, rendered on the IGate page. English: "Local gate applied only to RF ->
 * Internet, independent of the payload-type filter above...". */
#define TR_NOTE_RANGE_PREFIX                                                                                                                                   \
    "Filtro locale applicato solo a RF -> Internet, indipendente dal filtro per tipo di payload sopra. La distanza è misurata dalla posizione della Mia "      \
    "Stazione; i pacchetti la cui posizione non può essere decodificata non sono influenzati dal filtro di distanza."

/** Italian text for the form label for the "3rdparty unwrap en" field or fieldset, rendered on the IGate page. English: "Relay whitelisted third-party (})
 * traffic". */
#define TR_F_3RDPARTY_UNWRAP_EN "Inoltra traffico di terze parti (}) in lista bianca"
/** Italian text for the explanatory note shown beside the 3rdparty unwrap setting, rendered on the IGate page. English: "Off by default. Only takes effect when
 * the Internet to RF Callsign Filter above is set...". */
#define TR_NOTE_3RDPARTY_UNWRAP                                                                                                                                \
    "Disattivato per impostazione predefinita. Ha effetto solo quando il Filtro Nominativi Internet verso RF sopra è impostato su Lista Bianca: un pacchetto " \
    "incapsulato di terze parti viene decapsulato e inoltrato solo se il nominativo sorgente interno è a sua volta in lista bianca. Abilitare solo se ci si "  \
    "fida della fonte specifica e questa è stata inserita in lista bianca - ri-filtrare il traffico di terze parti senza questa restrizione è la causa più "   \
    "comune dei loop IGate."

/** Italian text for the form label for the "satgate" field or fieldset, rendered on the IGate page. English: "Satellite Gate List". */
#define TR_F_SATGATE "Elenco Digipeater Satellitari"
/** Italian text for the form label for the "satgate call" field or fieldset, rendered on the IGate page. English: "Satellite Callsign". */
#define TR_F_SATGATE_CALL "Nominativo Satellite"
/** Italian text for the explanatory note shown beside the satgate setting, rendered on the IGate page. English: "Callsigns of satellite/ISS digipeaters (e.g.
 * ISS, PSAT). A frame routed through one of...". */
#define TR_NOTE_SATGATE                                                                                                                                        \
    "Nominativi dei digipeater satellitari/ISS (es. ISS, PSAT). Un pacchetto instradato tramite uno di questi viene inoltrato ad APRS-IS solo se la voce di "  \
    "percorso del digipeater è effettivamente contrassegnata come usata. Fino a 8 voci; lasciare vuota una voce per disattivarla."

/** Italian text for the form label for the "dup cache" field or fieldset, rendered on the IGate page. English: "Duplicate Suppression". */
#define TR_F_DUP_CACHE "Soppressione Duplicati"
/** Italian text for the form label for the "dup cache size" field or fieldset, rendered on the IGate page. English: "Cache Size (entries)". */
#define TR_F_DUP_CACHE_SIZE "Dimensione Cache (voci)"
/** Italian text for the form label for the "dup cache timeout ms" field or fieldset, rendered on the IGate page. English: "Suppression Window (ms)". */
#define TR_F_DUP_CACHE_TIMEOUT_MS "Finestra di Soppressione (ms)"
/** Italian text for the explanatory note shown beside the dup cache setting, rendered on the IGate page. English: "Shared by the IGate and the Digipeater to
 * suppress repeated copies of the same frame. A...". */
#define TR_NOTE_DUP_CACHE                                                                                                                                      \
    "Condiviso da IGate e Digipeater per sopprimere copie ripetute dello stesso pacchetto. Un digipeater molto attivo su una frequenza congestionata "         \
    "potrebbe richiedere una cache più grande; un IGate rurale con poco traffico potrebbe preferire una finestra più breve."

/** Italian text for the form label for the "msg gating" field or fieldset, rendered on the IGate page. English: "Message Gating (Internet to RF)". */
#define TR_F_MSG_GATING "Filtraggio Messaggi (Internet verso RF)"
/** Italian text for the form label for the "msg gate en" field or fieldset, rendered on the IGate page. English: "Apply message gating criteria". */
#define TR_F_MSG_GATE_EN "Applica i criteri di filtraggio dei messaggi"
/** Italian text for the form label for the "msg local window s" field or fieldset, rendered on the IGate page. English: "Heard-locally window (s)". */
#define TR_F_MSG_LOCAL_WINDOW_S "Finestra di ascolto locale (s)"
/** Italian text for the explanatory note shown beside the msg gating setting, rendered on the IGate page. English: "A message read from APRS-IS is put on the
 * air only when its addressee was heard on RF i...". */
#define TR_NOTE_MSG_GATING                                                                                                                                     \
    "Un messaggio letto da APRS-IS viene trasmesso solo se il destinatario è stato ascoltato in RF entro la finestra, il mittente no, l'intestazione del "     \
    "mittente non contiene TCPXX/NOGATE/RFONLY e il destinatario non è a sua volta su Internet. Anche il primo rapporto di posizione di quel destinatario "    \
    "viene ritrasmesso una volta, per poterlo localizzare. Disattivandolo si trasmette ogni messaggio consentito dal filtro dei tipi, verso destinatari in "   \
    "qualsiasi parte del mondo."

/** Italian text for the form label for the "digi aliases" field or fieldset, rendered on the IGate page. English: "n-N Path Aliases". */
#define TR_F_DIGI_ALIASES "Alias di Percorso n-N"
/** Italian text for the form label for the "digi alias" field or fieldset, rendered on the IGate page. English: "Alias". */
#define TR_F_DIGI_ALIAS "Alias"
/** Italian text for the form label for the "digi max n" field or fieldset, rendered on the IGate page. English: "Max N". */
#define TR_F_DIGI_MAX_N "N massimo"
/** Italian text for the form label for the "digi alias mode" field or fieldset, rendered on the IGate page. English: "Mode". */
#define TR_F_DIGI_ALIAS_MODE "Modalità"
/** Italian text for the form label for the "digi fillin only" field or fieldset, rendered on the IGate page. English: "Fill-in digipeater (single hop only)".
 */
#define TR_F_DIGI_FILLIN_ONLY "Digipeater di riempimento (un solo salto)"
/** Italian text for the form label for the "digi trap action" field or fieldset, rendered on the IGate page. English: "Hop count above Max N". */
#define TR_F_DIGI_TRAP_ACTION "Salti oltre il N massimo"
/** Italian text for the digipeater page label for trap clamp, rendered on the IGate page. English: "Clamp to Max N". */
#define TR_DIGI_TRAP_CLAMP "Limita al N massimo"
/** Italian text for the digipeater page label for trap drop, rendered on the IGate page. English: "Drop the frame". */
#define TR_DIGI_TRAP_DROP "Scarta la trama"
/** Italian text for the digipeater page label for mode off, rendered on the IGate page. English: "Off". */
#define TR_DIGI_MODE_OFF "Spento"
/** Italian text for the digipeater page label for mode trace, rendered on the IGate page. English: "Trace (insert callsign)". */
#define TR_DIGI_MODE_TRACE "Traccia (inserisce nominativo)"
/** Italian text for the digipeater page label for mode flood, rendered on the IGate page. English: "Flood (no callsign)". */
#define TR_DIGI_MODE_FLOOD "Inondazione (senza nominativo)"
/** Italian text for the form label for the "digi preempt" field or fieldset, rendered on the IGate page. English: "Explicit routes naming this station". */
#define TR_F_DIGI_PREEMPT "Percorsi espliciti che nominano questa stazione"
/** Italian text for the digipeater page label for preempt off, rendered on the IGate page. English: "Off (first unused address only)". */
#define TR_DIGI_PREEMPT_OFF "Spento (solo il primo indirizzo inutilizzato)"
/** Italian text for the digipeater page label for preempt mark, rendered on the IGate page. English: "Serve now, keep the skipped addresses". */
#define TR_DIGI_PREEMPT_MARK "Servire subito, mantenendo gli indirizzi saltati"
/** Italian text for the digipeater page label for preempt drop, rendered on the IGate page. English: "Serve now, discard the skipped addresses". */
#define TR_DIGI_PREEMPT_DROP "Servire subito, scartando gli indirizzi saltati"
/** Italian text for the form label for the "digi dest ssid" field or fieldset, rendered on the IGate page. English: "Digipeat by destination SSID (legacy)". */
#define TR_F_DIGI_DEST_SSID "Ripetizione tramite SSID di destinazione (legacy)"
/** Italian text for the explanatory note shown beside the digi aliases setting, rendered on the IGate page. English: "The only aliases this digipeater honours.
 * Write each one without its SSID; '#' matches...". */
#define TR_NOTE_DIGI_ALIASES                                                                                                                                   \
    "Gli unici alias che questo digipeater onora. Scrivere ciascuno senza SSID; '#' corrisponde a una sola cifra, quindi 'WIDE#' copre l'intera famiglia "     \
    "WIDEn. Le righe vengono provate in ordine e vince la prima corrispondenza. Traccia inserisce il nominativo di questa stazione affinché ogni salto sia "   \
    "identificabile in seguito, come richiede WIDEn-N; inondazione non lascia traccia e conviene solo a un alias regionale usato di proposito così."

/** Italian text for the explanatory note shown beside the digi preempt setting, rendered on the IGate page. English: "Off is the safe default. When it is on,
 * the path is scanned from its first unused address...". */
#define TR_NOTE_DIGI_PREEMPT                                                                                                                                   \
    "Spento è il valore sicuro predefinito. Acceso, il percorso viene scandito dal primo indirizzo inutilizzato fino alla fine cercando il nominativo di "     \
    "questa stazione o uno degli alias qui sopra che non sia un nome della famiglia n-N, e una corrispondenza trovata più avanti viene servita subito "        \
    "invece di attendere gli indirizzi che la precedono. È questo che fa funzionare un percorso esplicito come WIDE1-1,CITYA,WIDE2-1,CITYB, che carica il "    \
    "canale molto meno di un'inondazione WIDEn-N. Mantenere gli indirizzi saltati lascia visibile il percorso richiesto; scartarli mette in onda il percorso " \
    "residuo più breve. Gli alias n-N generici non vengono mai reclamati così in nessuna delle due modalità."

/** Italian text for the explanatory note shown beside the digi dest ssid setting, rendered on the IGate page. English: "Off by default. When on, a frame whose
 * AX.25 destination SSID is 1 to 7 is repeated on...". */
#define TR_NOTE_DIGI_DEST_SSID                                                                                                                                 \
    "Disattivato per impostazione predefinita. Quando è attivo, una trama il cui SSID di destinazione AX.25 sia da 1 a 7 viene ripetuta in base a quel "       \
    "solo SSID, prima di consultare la tabella degli alias qui sopra, quindi il percorso richiesto dalla stazione di origine viene ignorato. Attivarlo "       \
    "solo per un vicino datato che instrada ancora in questo modo."

/** Italian text for the symbol-picker label for icon, rendered on the IGate page. English: "Icon". */
#define TR_SYM_ICON "Icona"
/** Italian text for the symbol-picker label for quick pick, rendered on the IGate page. English: "Quick Pick". */
#define TR_SYM_QUICK_PICK "Selezione rapida"
/** Italian text for the symbol-picker label for primary table, rendered on the IGate page. English: "Primary Table ( / )". */
#define TR_SYM_PRIMARY_TABLE "Tabella primaria ( / )"
/** Italian text for the symbol-picker label for alternate table, rendered on the IGate page. English: "Alternate Table ( \ )". */
#define TR_SYM_ALTERNATE_TABLE "Tabella alternativa ( \\ )"
/** Italian text for the symbol-picker label for tracker, rendered on the IGate page. English: "Tracker:". */
#define TR_SYM_TRACKER "Tracker:"

/** @} */

#endif // LANG_IT_H
