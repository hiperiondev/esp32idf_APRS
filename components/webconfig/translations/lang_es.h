/**
 * @file lang_es.h
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
 * @brief Spanish strings.
 *
 * Only ever included by translations.h when LANGUAGE == LANG_ES. Keep this file's
 * TR_xxx macro list identical (same names) across all lang_xx.h files - only the
 * string contents should differ.
 */

#ifndef LANG_ES_H
#define LANG_ES_H

/**
 * @name Brand / chrome
 * @{
 */
/** Spanish text for the product name shown in the page header and browser title bar. English: "esp32idf_APRS Web Admin". */
#define TR_BRAND "Administración Web esp32idf_APRS"
/** Spanish text for the caption of the log-out link in the page header. English: "Logout". */
#define TR_LOGOUT "Cerrar sesión"
/** Spanish text for the heading of the page shown after a successful log-out. English: "Logged out". */
#define TR_LOGGED_OUT_TITLE "Sesión cerrada"
/** Spanish text for the caption of the link back to the login prompt after logging out. English: "Log in again". */
#define TR_LOG_IN_AGAIN "Iniciar sesión de nuevo"
/** Spanish text for the body of the HTTP 401 response sent when authentication fails. English: "401 Unauthorized". */
#define TR_UNAUTHORIZED "401 No autorizado"
/** Spanish text for the body of the HTTP 403 response sent when a POST fails the cross-site request check. English: "403 Forbidden: request origin could not be
 * verified". */
#define TR_FORBIDDEN_CSRF "403 Prohibido: no se pudo verificar el origen de la solicitud"
/** Spanish text for the interstitial shown after a successful save, while the browser is redirected back to the form. English: "Saved. Redirecting...". */
#define TR_SAVED_REDIRECT "Guardado. Redirigiendo..."
/** Spanish text for the warning shown when settings were accepted but could not be committed to flash. English: "Save failed: the settings could not be written
 * to flash. They are still in effect until...". */
#define TR_SAVE_FAILED "Error al guardar: no se pudieron escribir los ajustes en la memoria flash. Siguen activos hasta el próximo reinicio."

/** @} */

/**
 * @name Sidebar menu
 * @{
 */
/** Spanish text for the sidebar navigation entry for the Dashboard page, rendered on the sidebar. English: "Dashboard". */
#define TR_MENU_DASHBOARD "Panel"
/** Spanish text for the sidebar navigation entry for the Send/Receive Message page, rendered on the sidebar. English: "Snd/Rcv Msg". */
#define TR_MENU_MSGCHAT "Env/Rec Msj"
/** Spanish text for the sidebar navigation entry for the Bulletins page, rendered on the sidebar. English: "Bulletins". */
#define TR_MENU_BULLETINS "Boletines"
/** Spanish text for the sidebar navigation entry for the Objects and Items page, rendered on the sidebar. English: "Objects and Items". */
#define TR_MENU_OBJITEMS "Objetos e Ítems"
/** Spanish text for the sidebar navigation entry for the Station page, rendered on the sidebar. English: "Station". */
#define TR_MENU_STATION "Estación"
/** Spanish text for the sidebar navigation entry for the Radiomodem page, rendered on the sidebar. English: "Radiomodem". */
#define TR_MENU_RADIO "Radiomódem"
/** Spanish text for the sidebar navigation entry for the Message page, rendered on the sidebar. English: "Message". */
#define TR_MENU_MSG "Mensaje"
/** Spanish text for the sidebar navigation entry for the Query page, rendered on the sidebar. English: "Query". */
#define TR_MENU_QUERY "Consulta"
/** Spanish text for the sidebar navigation entry for the IGate page, rendered on the sidebar. English: "IGate". */
#define TR_MENU_IGATE "IGate"
/** Spanish text for the sidebar navigation entry for the Digipeater page, rendered on the sidebar. English: "Digipeater". */
#define TR_MENU_DIGI "Digirepetidor"
/** Spanish text for the sidebar navigation entry for the Tracker page, rendered on the sidebar. English: "Tracker". */
#define TR_MENU_TRACKER "Rastreador"
/** Spanish text for the sidebar navigation entry for the Weather page, rendered on the sidebar. English: "Weather". */
#define TR_MENU_WX "Meteorología"
/** Spanish text for the sidebar navigation entry for the Telemetry page, rendered on the sidebar. English: "Telemetry". */
#define TR_MENU_TLM "Telemetría"
/** Spanish text for the sidebar navigation entry for the GPS page, rendered on the sidebar. English: "GPS". */
#define TR_MENU_GPS "GPS"
/** Sidebar navigation entry for the Telegram page, rendered on the sidebar. */
#define TR_MENU_TELEGRAM "Telegram"
/** Spanish text for the sidebar navigation entry for the Logs page, rendered on the sidebar. English: "Logs". */
#define TR_MENU_LOGS "Registros"
/** Spanish text for the sidebar navigation entry for the System page, rendered on the sidebar. English: "System". */
#define TR_MENU_SYSTEM "Sistema"
/** Spanish text for the sidebar navigation entry for the Wireless page, rendered on the sidebar. English: "Wireless". */
#define TR_MENU_WIRELESS "Inalámbrico"
/** Spanish text for the sidebar navigation entry for the File Storage page, rendered on the sidebar. English: "File Storage". */
#define TR_MENU_STORAGE "Almacenamiento de archivos"
/** Spanish text for the sidebar navigation entry for the Firmware page, rendered on the sidebar. English: "Firmware". */
#define TR_MENU_ABOUT "Firmware"

/** @} */

/**
 * @name Common buttons / widgets
 * @{
 */
/** Spanish text for the caption of the "save" button, rendered on any page. English: "Save". */
#define TR_BTN_SAVE "Guardar"
/** Spanish text for the caption of the "auto generate" button, rendered on any page. English: "Auto Generate". */
#define TR_BTN_AUTO_GENERATE "Generar automáticamente"
/** Spanish text for the caption of the "loop test" button, rendered on any page. English: "LOOP TEST". */
#define TR_BTN_LOOP_TEST "PRUEBA DE BUCLE"
/** Spanish text for the loopback-test status message: saving, rendered on any page. English: "Saving settings...". */
#define TR_LOOPTEST_SAVING "Guardando..."
/** Spanish text for the loopback-test status message: running, rendered on any page. English: "Testing...". */
#define TR_LOOPTEST_RUNNING "Probando..."
/** Spanish text for the loopback-test status message: failed, rendered on any page. English: "Request failed". */
#define TR_LOOPTEST_FAILED "Fallo en la solicitud"
/** Spanish text for the caption of the checkbox that reveals a masked password field. English: "Show password". */
#define TR_SHOW_PASSWORD "Mostrar contraseña"

/** @} */

/**
 * @name page_about.c
 * @{
 */
/** Spanish text for the firmware page label for title, rendered on the Firmware page. English: "Firmware". */
#define TR_ABOUT_TITLE "Firmware"
/** Spanish text for the firmware page label for fw legend, rendered on the Firmware page. English: "Firmware". */
#define TR_ABOUT_FW_LEGEND "Firmware"
/** Spanish text for the firmware page label for project, rendered on the Firmware page. English: "Project:". */
#define TR_ABOUT_PROJECT "Proyecto:"
/** Spanish text for the firmware page label for version, rendered on the Firmware page. English: "Version:". */
#define TR_ABOUT_VERSION "Versión:"
/** Spanish text for the firmware page label for build date, rendered on the Firmware page. English: "Build date:". */
#define TR_ABOUT_BUILD_DATE "Fecha de compilación:"
/** Spanish text for the firmware page label for idf version, rendered on the Firmware page. English: "IDF version:". */
#define TR_ABOUT_IDF_VERSION "Versión de IDF:"
/** Spanish text for the firmware page label for partition, rendered on the Firmware page. English: "Running partition:". */
#define TR_ABOUT_PARTITION "Partición en ejecución:"
/** Spanish text for the firmware page label for ota legend, rendered on the Firmware page. English: "OTA Update". */
#define TR_ABOUT_OTA_LEGEND "Actualización OTA"
/** Spanish text for the firmware page label for ota body, rendered on the Firmware page. English: "Upload a new firmware .bin built for this board. It is
 * written to the inactive OTA slot...". */
#define TR_ABOUT_OTA_BODY                                                                                                                                      \
    "Cargue un nuevo firmware .bin compilado para esta placa. Se escribe en la "                                                                               \
    "partición OTA inactiva mientras el dispositivo sigue funcionando con la "                                                                                 \
    "actual; el dispositivo solo cambia y se reinicia una vez que la carga "                                                                                   \
    "finaliza y se verifica. Si el nuevo firmware no arranca correctamente, "                                                                                  \
    "se revierte automáticamente en el próximo reinicio."
/** Spanish text for the firmware-update label or status message for target slot, rendered on the Firmware page. English: "Target slot:". */
#define TR_OTA_TARGET_SLOT "Partición destino:"
/** Spanish text for the firmware-update label or status message for select file, rendered on the Firmware page. English: "Firmware file (.bin):". */
#define TR_OTA_SELECT_FILE "Archivo de firmware (.bin):"
/** Spanish text for the firmware-update label or status message for upload btn, rendered on the Firmware page. English: "Upload &amp; Flash". */
#define TR_OTA_UPLOAD_BTN "Cargar y grabar"
/** Spanish text for the firmware-update label or status message for confirm, rendered on the Firmware page. English: "Upload and flash this firmware? The
 * device will reboot when done.". */
#define TR_OTA_CONFIRM "¿Cargar y grabar este firmware? El dispositivo se reiniciará al finalizar."
/** Spanish text for the firmware-update label or status message for no file selected, rendered on the Firmware page. English: "Choose a firmware .bin file
 * first.". */
#define TR_OTA_NO_FILE_SELECTED "Seleccione primero un archivo de firmware .bin."
/** Spanish text for the firmware-update label or status message for uploading, rendered on the Firmware page. English: "Uploading and writing to flash...". */
#define TR_OTA_UPLOADING "Cargando y escribiendo en la memoria flash..."
/** Spanish text for the firmware-update label or status message for no partition, rendered on the Firmware page. English: "No OTA update slot is available on
 * this device's partition table. Reflash it once over...". */
#define TR_OTA_NO_PARTITION                                                                                                                                    \
    "No hay una partición OTA disponible en la tabla de particiones de este dispositivo. Vuelva a grabarlo una vez por USB/UART con el partitions.csv actual " \
    "para habilitar OTA."
/** Spanish text for the firmware-update label or status message for begin failed, rendered on the Firmware page. English: "Could not start the OTA write:". */
#define TR_OTA_BEGIN_FAILED "No se pudo iniciar la escritura OTA: "
/** Spanish text for the firmware-update label or status message for no file chosen, rendered on the Firmware page. English: "no file was received". */
#define TR_OTA_NO_FILE_CHOSEN "no se recibió ningún archivo"
/** Spanish text for the firmware-update label or status message for upload failed, rendered on the Firmware page. English: "Firmware upload failed". */
#define TR_OTA_UPLOAD_FAILED "La carga del firmware falló"
/** Spanish text for the firmware-update label or status message for validate failed, rendered on the Firmware page. English: "image validation failed - the
 * file is not a valid firmware image for this board". */
#define TR_OTA_VALIDATE_FAILED "la validación de la imagen falló: el archivo no es una imagen de firmware válida para esta placa"
/** Spanish text for the firmware-update label or status message for success, rendered on the Firmware page. English: "Firmware written and verified
 * successfully.". */
#define TR_OTA_SUCCESS "Firmware escrito y verificado correctamente."
/** Spanish text for the firmware-update label or status message for rebooting, rendered on the Firmware page. English: "Rebooting into the new firmware
 * now...". */
#define TR_OTA_REBOOTING "Reiniciando con el nuevo firmware..."

/** @} */

/**
 * @name Common field/fieldset labels (auto-extracted from pages/<page>.c source files)
 * @{
 */
/** Spanish text for the form label for the "add timestamp" field or fieldset, rendered on the configuration forms. English: "Add timestamp". */
#define TR_F_ADD_TIMESTAMP "Añadir marca de tiempo"
/** Spanish text for the form label for the "altitude m" field or fieldset, rendered on the configuration forms. English: "Altitude (m)". */
#define TR_F_ALTITUDE_M "Altitud (m)"
/** Spanish text for the form label for the "aprs is server" field or fieldset, rendered on the configuration forms. English: "APRS-IS Server". */
#define TR_F_APRS_IS_SERVER "Servidor APRS-IS"
/** Spanish text for the form label for the "aprs messaging" field or fieldset, rendered on the configuration forms. English: "APRS Messaging". */
#define TR_F_APRS_MESSAGING "Mensajería APRS"
/** Spanish text for the form label for the "aprs passcode" field or fieldset, rendered on the configuration forms. English: "APRS-IS Passcode". */
#define TR_F_APRS_PASSCODE "Código de acceso APRS-IS"
/** Spanish text for the form label for the "aprs symbols" field or fieldset, rendered on the configuration forms. English: "APRS Symbols". */
#define TR_F_APRS_SYMBOLS "Símbolos APRS"
/** Spanish text for the form label for the "audio afsk" field or fieldset, rendered on the configuration forms. English: "Audio / AFSK". */
#define TR_F_AUDIO_AFSK "Audio / AFSK"
/** Spanish text for the form label for the "enable audio modem" field or fieldset, rendered on the configuration forms. English: "Enable audio ADC/DAC modem".
 */
#define TR_F_ENABLE_AUDIO_MODEM "Activar módem ADC/DAC de audio"
/** Spanish text for the form label for the "afsk modulation" field or fieldset, rendered on the configuration forms. English: "Modulation". */
#define TR_F_AFSK_MODULATION "Modulación"
/** Spanish text for the form label for the "audio low pass filter" field or fieldset, rendered on the configuration forms. English: "Audio low-pass filter". */
#define TR_F_AUDIO_LOW_PASS_FILTER "Filtro paso bajo de audio"
/** Spanish text for the form label for the "beacon interval s" field or fieldset, rendered on the configuration forms. English: "Beacon interval (s)". */
#define TR_F_BEACON_INTERVAL_S "Intervalo de baliza (s)"
/** Spanish text for the form label for the "beacon position" field or fieldset, rendered on the configuration forms. English: "Beacon / Position". */
#define TR_F_BEACON_POSITION "Baliza / Posición"
/** Spanish text for the form label for the "beacon position 2" field or fieldset, rendered on the configuration forms. English: "Beacon position". */
#define TR_F_BEACON_POSITION_2 "Posición de baliza"
/** Spanish text for the form label for the "beacon via internet" field or fieldset, rendered on the configuration forms. English: "Beacon via Internet". */
#define TR_F_BEACON_VIA_INTERNET "Baliza vía Internet"
/** Spanish text for the form label for the "beacon via rf" field or fieldset, rendered on the configuration forms. English: "Beacon via RF". */
#define TR_F_BEACON_VIA_RF "Baliza vía RF"
/** Spanish text for the form label for the "comment" field or fieldset, rendered on the configuration forms. English: "Comment". */
#define TR_F_COMMENT "Comentario"
/** Spanish text for the form label for the "compress position" field or fieldset, rendered on the configuration forms. English: "Compress position". */
#define TR_F_COMPRESS_POSITION "Comprimir posición"
/** Spanish text for the form label for the "dashboard" field or fieldset, rendered on the configuration forms. English: "Dashboard". */
#define TR_F_DASHBOARD "Panel"
/** Spanish text for the form label for the "data interval s" field or fieldset, rendered on the configuration forms. English: "Data interval (s)". */
#define TR_F_DATA_INTERVAL_S "Intervalo de datos (s)"
/** Spanish text for the form label for the "digipeater" field or fieldset, rendered on the configuration forms. English: "Digipeater". */
#define TR_F_DIGIPEATER "Digipeater"
/** Spanish text for the form label for the "enable" field or fieldset, rendered on the configuration forms. English: "Enable". */
#define TR_F_ENABLE "Habilitar"
/** Spanish text for the form label for the "enable digipeater" field or fieldset, rendered on the configuration forms. English: "Enable Digipeater". */
#define TR_F_ENABLE_DIGIPEATER "Habilitar Digipeater"
/** Spanish text for the form label for the "enable igate" field or fieldset, rendered on the configuration forms. English: "Enable IGate". */
#define TR_F_ENABLE_IGATE "Habilitar IGate"
/** Spanish text for the form label for the "enable messaging" field or fieldset, rendered on the configuration forms. English: "Enable messaging". */
#define TR_F_ENABLE_MESSAGING "Habilitar mensajería"
/** Spanish text for the form label for the "enable tracker" field or fieldset, rendered on the configuration forms. English: "Enable Tracker". */
#define TR_F_ENABLE_TRACKER "Habilitar Tracker"
/** Spanish text for the form label for the "enable wx" field or fieldset, rendered on the configuration forms. English: "Enable WX". */
#define TR_F_ENABLE_WX "Habilitar WX"
/** Spanish text for the form label for the "file storage" field or fieldset, rendered on the configuration forms. English: "File Storage". */
#define TR_F_FILE_STORAGE "Almacenamiento de archivos"
/** Spanish text for the form label for the "filter" field or fieldset, rendered on the configuration forms. English: "Filter". */
#define TR_F_FILTER "Filtro"
/** Spanish text for the form label for the "fixed altitude m" field or fieldset, rendered on the configuration forms. English: "Fixed Altitude (m)". */
#define TR_F_FIXED_ALTITUDE_M "Altitud fija (m)"
/** Spanish text for the form label for the "fixed interval s" field or fieldset, rendered on the configuration forms. English: "Fixed interval (s)". */
#define TR_F_FIXED_INTERVAL_S "Intervalo fijo (s)"
/** Spanish text for the form label for the "fixed latitude" field or fieldset, rendered on the configuration forms. English: "Fixed Latitude". */
#define TR_F_FIXED_LATITUDE "Latitud fija"
/** Spanish text for the form label for the "fixed longitude" field or fieldset, rendered on the configuration forms. English: "Fixed Longitude". */
#define TR_F_FIXED_LONGITUDE "Longitud fija"
/** Spanish text for the form label for the "fx 25 forward error corrected ax 25" field or fieldset, rendered on the configuration forms. English: "FX.25
 * (forward-error-corrected AX.25)". */
#define TR_F_FX_25_FORWARD_ERROR_CORRECTED_AX_25 "FX.25 (AX.25 con corrección de errores)"
/** Spanish text for the form label for the "igate" field or fieldset, rendered on the configuration forms. English: "IGate". */
#define TR_F_IGATE "IGate"
/** Spanish text for the form label for the "include altitude" field or fieldset, rendered on the configuration forms. English: "Include altitude". */
#define TR_F_INCLUDE_ALTITUDE "Incluir altitud"
/** Form label for the "tracker phg" field or fieldset, rendered on the Tracker page. */
#define TR_F_TRACKER_PHG "Incluir extension de datos PHG"
/** Spanish text for the form label for the "internet to rf" field or fieldset, rendered on the configuration forms. English: "Internet to RF". */
#define TR_F_INTERNET_TO_RF "Internet a RF"
/** Spanish text for the form label for the "latitude" field or fieldset, rendered on the configuration forms. English: "Latitude". */
#define TR_F_LATITUDE "Latitud"
/** Spanish text for the form label for the "longitude" field or fieldset, rendered on the configuration forms. English: "Longitude". */
#define TR_F_LONGITUDE "Longitud"
/** Spanish text for the form label for the "message" field or fieldset, rendered on the configuration forms. English: "Message". */
#define TR_F_MESSAGE "Mensaje"
/** Spanish text for the form label for the "message alarm enable" field or fieldset, rendered on the configuration forms. English: "Enable Message Alarm". */
#define TR_F_MESSAGE_ALARM_ENABLE "Activar alarma de mensajes"
/** Spanish text for the form label for the "message alarm pin" field or fieldset, rendered on the configuration forms. English: "Message Alarm pin". */
#define TR_F_MESSAGE_ALARM_PIN "Pin de alarma de mensajes"
/** Spanish text for the fieldset label for the operator-defined message-group name slots on the Message page. English: "Message Groups". */
#define TR_F_MESSAGE_GROUPS "Grupos de mensajes"
/** Spanish text for the form label for one operator-defined message-group name slot, "%d" is the slot number (1-based). English: "Group %d". */
#define TR_F_MESSAGE_GROUP_FMT "Grupo %d"
/** Spanish text for the option label for the Mic-E position comment M0 (Off Duty), rendered on the Tracker page. English: "M0 Off Duty". */
#define TR_F_MICE_MSG_M0 "M0 Fuera de servicio"
/** Spanish text for the option label for the Mic-E position comment M1 (En Route), rendered on the Tracker page. English: "M1 En Route". */
#define TR_F_MICE_MSG_M1 "M1 En ruta"
/** Spanish text for the option label for the Mic-E position comment M2 (In Service), rendered on the Tracker page. English: "M2 In Service". */
#define TR_F_MICE_MSG_M2 "M2 En servicio"
/** Spanish text for the option label for the Mic-E position comment M3 (Returning), rendered on the Tracker page. English: "M3 Returning". */
#define TR_F_MICE_MSG_M3 "M3 Regresando"
/** Spanish text for the option label for the Mic-E position comment M4 (Committed), rendered on the Tracker page. English: "M4 Committed". */
#define TR_F_MICE_MSG_M4 "M4 Comprometido"
/** Spanish text for the option label for the Mic-E position comment M5 (Special), rendered on the Tracker page. English: "M5 Special". */
#define TR_F_MICE_MSG_M5 "M5 Especial"
/** Spanish text for the option label for the Mic-E position comment M6 (Priority), rendered on the Tracker page. English: "M6 Priority". */
#define TR_F_MICE_MSG_M6 "M6 Prioritario"
/** Spanish text for the word prefixed to the number of each locally defined Mic-E position comment C0-C6, rendered on the Tracker page. English: "Custom". */
#define TR_F_MICE_MSG_CUSTOM "Personalizado"
/** Spanish text for the form label for the "mice position" field or fieldset, rendered on the configuration forms. English: "Mic-E position encoding". */
#define TR_F_MICE_POSITION "Codificación de posición Mic-E"
/** Spanish text for the form label for the "mice position comment" field or fieldset, rendered on the configuration forms. English: "Mic-E position comment".
 */
#define TR_F_MICE_POSITION_COMMENT "Comentario de posición Mic-E"
/** Spanish text for the form label for the "mode" field or fieldset, rendered on the configuration forms. English: "Mode". */
#define TR_F_MODE "Modo"
/** Spanish text for the form label for the "my callsign" field or fieldset, rendered on the configuration forms. English: "My Callsign". */
#define TR_F_MY_CALLSIGN "Mi indicativo"
/** Spanish text for the caption of the control that copies the station identity into the current form. English: "Use My Station Data". */
#define TR_USE_MY_STATION_DATA "Usar mis datos de estación"
/** Spanish text for the caption of the control that fills the current form's position/motion fields live from the GNSS receiver. English: "Use GPS". */
#define TR_USE_GPS_DATA "Usar GPS"
/** Spanish text for the form label for the Tracker page checkbox that has the tracker beacon read the GNSS receiver at every transmission instead of
    beaconing the fixed position above. English: "Use live GPS fix". */
#define TR_F_TRACKER_USE_LIVE_GPS "Usar posición GPS en vivo"
/** Spanish text for the form label for the "name" field or fieldset, rendered on the configuration forms. English: "Name". */
#define TR_F_NAME "Nombre"
/** Spanish text for the form label for the "object item name" field or fieldset, rendered on the configuration forms. English: "Object/Item name". */
#define TR_F_OBJECT_ITEM_NAME "Nombre del objeto/ítem"
/** Spanish text for the form label for the "object name" field or fieldset, rendered on the configuration forms. English: "Object name". */
#define TR_F_OBJECT_NAME "Nombre del objeto"
/** Spanish text for the form label for the "options" field or fieldset, rendered on the configuration forms. English: "Options". */
#define TR_F_OPTIONS "Opciones"
/** Spanish text for the form label for the "parm unit eqns interval s" field or fieldset, rendered on the configuration forms. English: "PARM/UNIT/EQNS
 * interval (s)". */
#define TR_F_PARM_UNIT_EQNS_INTERVAL_S "Intervalo PARM/UNIT/EQNS (s)"
/** Spanish text for the form label for the "password" field or fieldset, rendered on the configuration forms. English: "Password". */
#define TR_F_PASSWORD "Contraseña"
/** Spanish text for the form label for the "position" field or fieldset, rendered on the configuration forms. English: "Position". */
#define TR_F_POSITION "Posición"
/** Spanish text for the form label for the "preamble ms" field or fieldset, rendered on the configuration forms. English: "Preamble (ms)". */
#define TR_F_PREAMBLE_MS "Preámbulo (ms)"
/** Spanish text for the selector entry meaning the feature or pin is switched off. English: "Disabled". */
#define TR_DISABLED "Deshabilitado"
/** Spanish text for the format string for a GPIO selector entry that is already claimed, taking the pin number and the claiming peripheral. English: "GPIO%d
 * (used: %.30s)". */
#define TR_GPIO_USED_BY "GPIO%d (usado: %.30s)"
/** Spanish text for the form label for the "protocol" field or fieldset, rendered on the configuration forms. English: "Protocol". */
#define TR_F_PROTOCOL "Protocolo"
/** Spanish text for the form label for the "query" field or fieldset, rendered on the configuration forms. English: "Query". */
#define TR_F_QUERY "Consulta"
/** Spanish text for the form label for the "enable query" field or fieldset, rendered on the configuration forms. English: "Enable query responder". */
#define TR_F_ENABLE_QUERY "Habilitar respondedor de consultas"
/** Spanish text for the form label for the "query rf" field or fieldset, rendered on the configuration forms. English: "Answer queries heard on RF". */
#define TR_F_QUERY_RF "Responder consultas escuchadas en RF"
/** Spanish text for the form label for the "query inet" field or fieldset, rendered on the configuration forms. English: "Answer queries heard from APRS-IS".
 */
#define TR_F_QUERY_INET "Responder consultas recibidas de APRS-IS"
/** Spanish text for the form label for the "query aprs" field or fieldset, rendered on the configuration forms. English: "?APRS? - general station query". */
#define TR_F_QUERY_APRS "?APRS? - consulta general de estación"
/** Spanish text for the form label for the "query wx" field or fieldset, rendered on the configuration forms. English: "?WX? - weather report request". */
#define TR_F_QUERY_WX "?WX? - solicitud de reporte meteorológico"
/** Spanish text for the form label for the "query igate" field or fieldset, rendered on the configuration forms. English: "?IGATE? - IGate status request". */
#define TR_F_QUERY_IGATE "?IGATE? - solicitud de estado del IGate"
/** Spanish text for the form label for the "query directed" field or fieldset, rendered on the configuration forms. English: "Directed queries (CALL:?query?)".
 */
#define TR_F_QUERY_DIRECTED "Consultas dirigidas (CALL:?query?)"
/** Spanish text for the form label for the "query ext" field or fieldset, rendered on the configuration forms. English: "Extended directed queries
 * (?APRSD/?APRSH/?APRSM/?APRSO/?APRSP/?APRSS/?APRST)". */
#define TR_F_QUERY_EXT "Consultas dirigidas extendidas (?APRSD/?APRSH/?APRSM/?APRSO/?APRSP/?APRSS/?APRST)"
/** Spanish text for the form label for the "query min interval" field or fieldset, rendered on the configuration forms. English: "Minimum seconds between
 * identical responses". */
#define TR_F_QUERY_MIN_INTERVAL "Segundos mínimos entre respuestas idénticas"
/** Spanish text for the form label for the "query cap section" field or fieldset, rendered on the configuration forms. English: "Station capabilities
 * beacon". */
#define TR_F_QUERY_CAP_SECTION "Baliza de capacidades de estación"
/** Spanish text for the form label for the "query cap enable" field or fieldset, rendered on the configuration forms. English: "Send capabilities
 * periodically". */
#define TR_F_QUERY_CAP_ENABLE "Enviar capacidades periódicamente"
/** Spanish text for the form label for the "query cap interval" field or fieldset, rendered on the configuration forms. English: "Capabilities beacon interval
 * (s)". */
#define TR_F_QUERY_CAP_INTERVAL "Intervalo de la baliza de capacidades (s)"
/** Spanish text for the form label for the "query cap extra" field or fieldset, rendered on the configuration forms. English: "Additional capability
 * tokens". */
#define TR_F_QUERY_CAP_EXTRA "Elementos de capacidad adicionales"
/** Spanish text for the form label for the "radio modem" field or fieldset, rendered on the configuration forms. English: "Radiomodem". */
#define TR_F_RADIO_MODEM "Radiomódem"
/** Spanish text for the form label for the "retry count" field or fieldset, rendered on the configuration forms. English: "Retry count". */
#define TR_F_RETRY_COUNT "Número de reintentos"
/** Spanish text for the form label for the "retry interval s" field or fieldset, rendered on the configuration forms. English: "Retry interval (s)". */
#define TR_F_RETRY_INTERVAL_S "Intervalo de reintento (s)"
/** Spanish text for the form label for the "rf to internet" field or fieldset, rendered on the configuration forms. English: "RF to Internet". */
#define TR_F_RF_TO_INTERNET "RF a Internet"
/** Spanish text for the form label for the "rf tx buffers" field or fieldset, rendered on the configuration forms. English: "TX buffers". */
#define TR_F_RF_TX_BUFFERS "Buffers de TX"
/** Spanish text for the form label for the "duty cycle en" field or fieldset, rendered on the configuration forms. English: "Duty-cycle limiter". */
#define TR_F_DUTY_CYCLE_EN "Limitador de ciclo de trabajo"
/** Spanish text for the form label for the "duty cycle pct" field or fieldset, rendered on the configuration forms. English: "Duty-cycle limit (%)". */
#define TR_F_DUTY_CYCLE_PCT "Límite de ciclo de trabajo (%)"
/** Spanish text for the form label for the "ptt min unkey ms" field or fieldset, rendered on the configuration forms. English: "PTT minimum unkey time (ms)".
 */
#define TR_F_PTT_MIN_UNKEY_MS "Tiempo mínimo de PTT liberado (ms)"
/** Spanish text for the form label for the "csma persistence" field or fieldset, rendered on the configuration forms. English: "CSMA persistence (p, 1-255)".
 */
#define TR_F_CSMA_PERSISTENCE "Persistencia CSMA (p, 1-255)"
/** Spanish text for the form label for the "send receive via internet" field or fieldset, rendered on the configuration forms. English: "Send/receive via
 * Internet". */
#define TR_F_SEND_RECEIVE_VIA_INTERNET "Enviar/recibir vía Internet"
/** Spanish text for the form label for the "send receive via rf" field or fieldset, rendered on the configuration forms. English: "Send/receive via RF". */
#define TR_F_SEND_RECEIVE_VIA_RF "Enviar/recibir vía RF"
/** Spanish text for the form label for the "send via internet" field or fieldset, rendered on the configuration forms. English: "Send via Internet". */
#define TR_F_SEND_VIA_INTERNET "Enviar vía Internet"
/** Spanish text for the form label for the "send via rf" field or fieldset, rendered on the configuration forms. English: "Send via RF". */
#define TR_F_SEND_VIA_RF "Enviar vía RF"
/** Spanish text for the form label for the "sensor mapping enable averaged source channel" field or fieldset, rendered on the configuration forms. English:
 * "Sensor Mapping (enable / averaged / source channel)". */
#define TR_F_SENSOR_MAPPING_ENABLE_AVERAGED_SOURCE_CHANNEL "Mapeo de sensores (habilitar / promediado / canal de origen)"
/** Spanish text for the form label for the "server host" field or fieldset, rendered on the configuration forms. English: "Server Host". */
#define TR_F_SERVER_HOST "Host del servidor"
/** Spanish text for the form label for the "server port" field or fieldset, rendered on the configuration forms. English: "Server Port". */
#define TR_F_SERVER_PORT "Puerto del servidor"
/** Spanish text for the fieldset label for the Tracker page's SmartBeaconing settings, rendered on the configuration forms. English: "SmartBeaconing". */
#define TR_F_SMARTBEACONING "SmartBeaconing"
/** Spanish text for the form label for the "enable smartbeaconing" checkbox, rendered on the Tracker page. English: "Enable SmartBeaconing". */
#define TR_F_SMARTBEACONING_ENABLE "Activar SmartBeaconing"
/** Spanish text for the form label for the SmartBeaconing slow-rate (stationary) beacon interval, rendered on the Tracker page. English: "Slow-rate
    interval (s)". */
#define TR_F_SMARTBEACONING_SLOW_INTERVAL_S "Intervalo lento (s)"
/** Spanish text for the form label for the SmartBeaconing fast-rate (moving) beacon interval, rendered on the Tracker page. English: "Fast-rate interval
    (s)". */
#define TR_F_SMARTBEACONING_FAST_INTERVAL_S "Intervalo rápido (s)"
/** Spanish text for the form label for the SmartBeaconing low-speed threshold, rendered on the Tracker page. English: "Low speed (km/h)". */
#define TR_F_SMARTBEACONING_LOW_SPEED_KMH "Velocidad baja (km/h)"
/** Spanish text for the form label for the SmartBeaconing high-speed threshold, rendered on the Tracker page. English: "High speed (km/h)". */
#define TR_F_SMARTBEACONING_HIGH_SPEED_KMH "Velocidad alta (km/h)"
/** Spanish text for the form label for the SmartBeaconing corner-pegging minimum turn angle, rendered on the Tracker page. English: "Turn angle (deg)". */
#define TR_F_SMARTBEACONING_TURN_ANGLE "Ángulo de giro (grados)"
/** Spanish text for the form label for the SmartBeaconing corner-pegging turn slope, rendered on the Tracker page. English: "Turn slope (deg)". */
#define TR_F_SMARTBEACONING_TURN_SLOPE "Pendiente de giro (grados)"
/** Spanish text for the form label for the SmartBeaconing corner-pegging minimum turn time, rendered on the Tracker page. English: "Minimum turn time
    (s)". */
#define TR_F_SMARTBEACONING_MIN_TURN_TIME_S "Tiempo mínimo entre giros (s)"
/** Spanish text for the form label for the "ssid" field or fieldset, rendered on the configuration forms. English: "SSID". */
#define TR_F_SSID "SSID"
/** Spanish text for the form label for the "station" field or fieldset, rendered on the configuration forms. English: "Station". */
#define TR_F_STATION "Estación"
/** Spanish text for the form label for the "bulletins" field or fieldset, rendered on the configuration forms. English: "Bulletins". */
#define TR_F_BULLETINS "Boletines"
/** Spanish text for the form label for the "bulletin fmt" field or fieldset, rendered on the configuration forms. English: "Bulletin %d". */
#define TR_F_BULLETIN_FMT "Boletín %d"
/** Spanish text for the form label for the "bulletin id" field or fieldset, rendered on the configuration forms. English: "Identifier (0-9 bulletin, A-Z
 * announcement)". */
#define TR_F_BULLETIN_ID "Identificador (0-9 boletin, A-Z anuncio)"
/** Spanish text for the form label for the "bulletin group" field or fieldset, rendered on the configuration forms. English: "Group (up to 5 chars, empty =
 * general)". */
#define TR_F_BULLETIN_GROUP "Grupo (hasta 5 caracteres, vacio = general)"
/** Spanish text for the form label for the "bulletin msg" field or fieldset, rendered on the configuration forms. English: "Message (max 67 chars)". */
#define TR_F_BULLETIN_MSG "Mensaje (máx 67 caract.)"
/** Spanish text for the form label for the "bulletin expire" field or fieldset, rendered on the configuration forms. English: "Expire (hours, 0 = never)". */
#define TR_F_BULLETIN_EXPIRE "Expira (horas, 0 = nunca)"
/** Spanish text for the form label for the "objitems" field or fieldset, rendered on the configuration forms. English: "Objects and Items". */
#define TR_F_OBJITEMS "Objetos e Ítems"
/** Spanish text for the form label for the "objitem fmt" field or fieldset, rendered on the configuration forms. English: "Object/Item %d". */
#define TR_F_OBJITEM_FMT "Objeto/Ítem %d"
/** Spanish text for the form label for the "objitem tx control" field or fieldset, rendered on the configuration forms. English: "Transmission Control". */
#define TR_F_OBJITEM_TX_CONTROL "Control de Transmisión"
/** Spanish text for the form label for the "objitem identity" field or fieldset, rendered on the configuration forms. English: "Identity & State". */
#define TR_F_OBJITEM_IDENTITY "Identidad y Estado"
/** Spanish text for the form label for the "objitem pos symbol" field or fieldset, rendered on the configuration forms. English: "Position & Symbol". */
#define TR_F_OBJITEM_POS_SYMBOL "Posición y Símbolo"
/** Spanish text for the form label for the "objitem area section" field or fieldset, rendered on the configuration forms. English: "Area Object". */
#define TR_F_OBJITEM_AREA_SECTION "Objeto de Área"
/** Spanish text for the form label for the "objitem signpost section" field or fieldset, rendered on the configuration forms. English: "Signpost". */
#define TR_F_OBJITEM_SIGNPOST_SECTION "Señal (Signpost)"
/** Spanish text for the form label for the "objitem repeater section" field or fieldset, rendered on the configuration forms. English: "Repeater Radio
 * Parameters". */
#define TR_F_OBJITEM_REPEATER_SECTION "Parámetros de Radio del Repetidor"
/** Spanish text for the form label for the "objitem timing section" field or fieldset, rendered on the configuration forms. English: "Beacon Timing". */
#define TR_F_OBJITEM_TIMING_SECTION "Temporización de Baliza"
/** Spanish text for the form label for the "objitem type" field or fieldset, rendered on the configuration forms. English: "Type". */
#define TR_F_OBJITEM_TYPE "Tipo"
/** Spanish text for the form label for the "objitem type object" field or fieldset, rendered on the configuration forms. English: "Object (timestamped)". */
#define TR_F_OBJITEM_TYPE_OBJECT "Objeto (con marca de tiempo)"
/** Spanish text for the form label for the "objitem type item" field or fieldset, rendered on the configuration forms. English: "Item (non-timestamped)". */
#define TR_F_OBJITEM_TYPE_ITEM "Ítem (sin marca de tiempo)"
/** Spanish text for the form label for the "objitem permanent" field or fieldset, rendered on the configuration forms. English: "Permanent (Object only,
 * 111111z)". */
#define TR_F_OBJITEM_PERMANENT "Permanente (solo Objeto, 111111z)"
/** Spanish text for the form label for the "objitem permanent note" field or fieldset, rendered on the configuration forms. English: "A permanent Object is
 * sent with the fixed 111111z timestamp instead of the live time, s...". */
#define TR_F_OBJITEM_PERMANENT_NOTE                                                                                                                            \
    "Un Objeto permanente se envía con la marca de tiempo fija 111111z en lugar de la hora actual, de modo que nunca es reemplazado por un Objeto homónimo "   \
    "de otra estación - solo la estación de origen puede actualizarlo o moverlo. No tiene efecto en un Ítem."
/** Spanish text for the form label for the "objitem active" field or fieldset, rendered on the configuration forms. English: "Active (uncheck = kill)". */
#define TR_F_OBJITEM_ACTIVE "Activo (desmarcar = eliminar)"
/** Spanish text for the form label for the "objitem scope" field or fieldset, rendered on the configuration forms. English: "Scope". */
#define TR_F_OBJITEM_SCOPE "Alcance"
/** Spanish text for the form label for the "objitem scope private" field or fieldset, rendered on the configuration forms. English: "Private (not
 * transmitted)". */
#define TR_F_OBJITEM_SCOPE_PRIVATE "Privado (no se transmite)"
/** Spanish text for the form label for the "objitem scope local" field or fieldset, rendered on the configuration forms. English: "Local (RF only)". */
#define TR_F_OBJITEM_SCOPE_LOCAL "Local (solo RF)"
/** Spanish text for the form label for the "objitem scope global" field or fieldset, rendered on the configuration forms. English: "Global (RF + Internet)". */
#define TR_F_OBJITEM_SCOPE_GLOBAL "Global (RF + Internet)"
/** Spanish text for the form label for the "objitem symbol" field or fieldset, rendered on the configuration forms. English: "Symbol / overlay". */
#define TR_F_OBJITEM_SYMBOL "Símbolo / superposición"
/** Spanish text for the form label for the "objitem course" field or fieldset, rendered on the configuration forms. English: "Course (deg, 0-359)". */
#define TR_F_OBJITEM_COURSE "Rumbo (grados, 0-359)"
/** Spanish text for the form label for the "objitem speed" field or fieldset, rendered on the configuration forms. English: "Speed (knots, 0 = omit)". */
#define TR_F_OBJITEM_SPEED "Velocidad (nudos, 0 = omitir)"
/** Spanish text for the form label for the "objitem area shape" field or fieldset, rendered on the configuration forms. English: "Area shape (\l symbol)". */
#define TR_F_OBJITEM_AREA_SHAPE "Forma de área (símbolo \\l)"
/** Spanish text for the form label for the "objitem shape circle" field or fieldset, rendered on the configuration forms. English: "Circle". */
#define TR_F_OBJITEM_SHAPE_CIRCLE "Círculo"
/** Spanish text for the form label for the "objitem shape line down right" field or fieldset, rendered on the configuration forms. English: "Line
 * (down/right)". */
#define TR_F_OBJITEM_SHAPE_LINE_DOWN_RIGHT "Línea (abajo/derecha)"
/** Spanish text for the form label for the "objitem shape ellipse" field or fieldset, rendered on the configuration forms. English: "Ellipse". */
#define TR_F_OBJITEM_SHAPE_ELLIPSE "Elipse"
/** Spanish text for the form label for the "objitem shape triangle" field or fieldset, rendered on the configuration forms. English: "Triangle". */
#define TR_F_OBJITEM_SHAPE_TRIANGLE "Triángulo"
/** Spanish text for the form label for the "objitem shape box" field or fieldset, rendered on the configuration forms. English: "Box". */
#define TR_F_OBJITEM_SHAPE_BOX "Caja"
/** Spanish text for the form label for the "objitem shape filled" field or fieldset, rendered on the configuration forms. English: "(filled)". */
#define TR_F_OBJITEM_SHAPE_FILLED " (rellena)"
/** Spanish text for the form label for the "objitem area color" field or fieldset, rendered on the configuration forms. English: "Area color (0-15)". */
#define TR_F_OBJITEM_AREA_COLOR "Color de área (0-15)"
/** Spanish text for the form label for the "objitem area lat off" field or fieldset, rendered on the configuration forms. English: "Area latitude offset
 * (deg)". */
#define TR_F_OBJITEM_AREA_LAT_OFF "Desplaz. latitud del área (grados)"
/** Spanish text for the form label for the "objitem area lon off" field or fieldset, rendered on the configuration forms. English: "Area longitude offset
 * (deg)". */
#define TR_F_OBJITEM_AREA_LON_OFF "Desplaz. longitud del área (grados)"
/** Spanish text for the form label for the "objitem shape line down left" field or fieldset, rendered on the configuration forms. English: "Line
 * (down/left)". */
#define TR_F_OBJITEM_SHAPE_LINE_DOWN_LEFT "Línea (abajo/izquierda)"
/** Spanish text for the form label for the "objitem area width" field or fieldset, rendered on the configuration forms. English: "Line corridor width
 * (miles, 0 = omit)". */
#define TR_F_OBJITEM_AREA_WIDTH "Ancho del corredor de línea (millas, 0 = omitir)"
/** Spanish text for the form label for the "objitem signpost" field or fieldset, rendered on the configuration forms. English: "Signpost text (\m symbol, 3
 * chars)". */
#define TR_F_OBJITEM_SIGNPOST "Texto de señal (símbolo \\m, 3 caract.)"
/** Spanish text for the form label for the "objitem freq" field or fieldset, rendered on the configuration forms. English: "Monitor frequency (MHz, 0 = none)".
 */
#define TR_F_OBJITEM_FREQ "Frecuencia de monitoreo (MHz, 0 = ninguna)"
/** Spanish text for the form label for the "objitem rx freq enable" field or fieldset, rendered on the configuration forms. English: "Independent receive
 * frequency". */
#define TR_F_OBJITEM_RX_FREQ_ENABLE "Frecuencia de recepción independiente"
/** Spanish text for the form label for the "objitem rx freq" field or fieldset, rendered on the configuration forms. English: "Receive frequency (MHz, split
 * TX/RX)". */
#define TR_F_OBJITEM_RX_FREQ "Frecuencia de recepción (MHz, TX/RX separados)"
/** Spanish text for the form label for the "objitem duplex" field or fieldset, rendered on the configuration forms. English: "Duplex direction". */
#define TR_F_OBJITEM_DUPLEX "Dirección dúplex"
/** Spanish text for the form label for the "objitem duplex simplex" field or fieldset, rendered on the configuration forms. English: "Simplex". */
#define TR_F_OBJITEM_DUPLEX_SIMPLEX "Símplex"
/** Spanish text for the form label for the "objitem duplex plus" field or fieldset, rendered on the configuration forms. English: "Plus (+)". */
#define TR_F_OBJITEM_DUPLEX_PLUS "Positivo (+)"
/** Spanish text for the form label for the "objitem duplex minus" field or fieldset, rendered on the configuration forms. English: "Minus (-)". */
#define TR_F_OBJITEM_DUPLEX_MINUS "Negativo (-)"
/** Spanish text for the form label for the "objitem offset" field or fieldset, rendered on the configuration forms. English: "Duplex offset (kHz)". */
#define TR_F_OBJITEM_OFFSET "Desplazamiento dúplex (kHz)"
/** Spanish text for the form label for the "objitem dcs enable" field or fieldset, rendered on the configuration forms. English: "Use DCS code instead of
 * CTCSS tone". */
#define TR_F_OBJITEM_DCS_ENABLE "Usar código DCS en vez de tono CTCSS"
/** Spanish text for the form label for the "objitem tone" field or fieldset, rendered on the configuration forms. English: "Subaudible tone CTCSS (Hz, 0 =
 * none)". */
#define TR_F_OBJITEM_TONE "Tono subaudible CTCSS (Hz, 0 = ninguno)"
/** Spanish text for the form label for the "objitem dcs code" field or fieldset, rendered on the configuration forms. English: "DCS code (octal, 0-511)". */
#define TR_F_OBJITEM_DCS_CODE "Código DCS (octal, 0-511)"
/** Spanish text for the form label for the "objitem narrow" field or fieldset, rendered on the configuration forms. English: "Narrowband modulation". */
#define TR_F_OBJITEM_NARROW "Modulación de banda estrecha"
/** Spanish text for the form label for the "objitem range" field or fieldset, rendered on the configuration forms. English: "Coverage range (0 = none)". */
#define TR_F_OBJITEM_RANGE "Alcance de cobertura (0 = ninguno)"
/** Spanish text for the form label for the "objitem range unit" field or fieldset, rendered on the configuration forms. English: "Range unit". */
#define TR_F_OBJITEM_RANGE_UNIT "Unidad de alcance"
/** Spanish text for the form label for the "objitem range unit mi" field or fieldset, rendered on the configuration forms. English: "Miles". */
#define TR_F_OBJITEM_RANGE_UNIT_MI "Millas"
/** Spanish text for the form label for the "objitem range unit km" field or fieldset, rendered on the configuration forms. English: "Kilometers". */
#define TR_F_OBJITEM_RANGE_UNIT_KM "Kilómetros"
/** Spanish text for the form label for the "objitem path fmt" field or fieldset, rendered on the configuration forms. English: "Path %d". */
#define TR_F_OBJITEM_PATH_FMT "Ruta %d"
/** Spanish text for the form label for the "objitem qru" field or fieldset, rendered on the configuration forms. English: "QRU group membership". */
#define TR_F_OBJITEM_QRU "Membresía de grupo QRU"
/** Spanish text for the form label for the "objitem qru none" field or fieldset, rendered on the configuration forms. English: "(none)". */
#define TR_F_OBJITEM_QRU_NONE "(ninguno)"
/** Spanish text for the form label for the "qru ambu" field or fieldset, rendered on the configuration forms. English: "ambulance". */
#define TR_F_QRU_AMBU "ambulancia"
/** Spanish text for the form label for the "qru club" field or fieldset, rendered on the configuration forms. English: "ham radio club". */
#define TR_F_QRU_CLUB "club de radioaficionados"
/** Spanish text for the form label for the "qru echo" field or fieldset, rendered on the configuration forms. English: "Echolink". */
#define TR_F_QRU_ECHO "Echolink"
/** Spanish text for the form label for the "qru fire" field or fieldset, rendered on the configuration forms. English: "fire station". */
#define TR_F_QRU_FIRE "estación de bomberos"
/** Spanish text for the form label for the "qru food" field or fieldset, rendered on the configuration forms. English: "restaurants". */
#define TR_F_QRU_FOOD "restaurantes"
/** Spanish text for the form label for the "qru fuel" field or fieldset, rendered on the configuration forms. English: "gas/petrol stations". */
#define TR_F_QRU_FUEL "estaciones de servicio/gasolineras"
/** Spanish text for the form label for the "qru hosp" field or fieldset, rendered on the configuration forms. English: "hospitals". */
#define TR_F_QRU_HOSP "hospitales"
/** Spanish text for the form label for the "qru lifeboat" field or fieldset, rendered on the configuration forms. English: "lifeboats". */
#define TR_F_QRU_LIFEBOAT "botes salvavidas"
/** Spanish text for the form label for the "qru lths" field or fieldset, rendered on the configuration forms. English: "lighthouses". */
#define TR_F_QRU_LTHS "faros"
/** Spanish text for the form label for the "qru poli" field or fieldset, rendered on the configuration forms. English: "police stations". */
#define TR_F_QRU_POLI "comisarías de policía"
/** Spanish text for the form label for the "qru post" field or fieldset, rendered on the configuration forms. English: "post offices". */
#define TR_F_QRU_POST "oficinas de correos"
/** Spanish text for the form label for the "qru rd13" field or fieldset, rendered on the configuration forms. English: "D-Star 13cm repeaters". */
#define TR_F_QRU_RD13 "repetidores D-Star 13cm"
/** Spanish text for the form label for the "qru rd23" field or fieldset, rendered on the configuration forms. English: "D-Star 23cm repeaters". */
#define TR_F_QRU_RD23 "repetidores D-Star 23cm"
/** Spanish text for the form label for the "qru rd2m" field or fieldset, rendered on the configuration forms. English: "D-Star 2m repeaters". */
#define TR_F_QRU_RD2M "repetidores D-Star 2m"
/** Spanish text for the form label for the "qru rd3c" field or fieldset, rendered on the configuration forms. English: "D-Star 3cm repeaters". */
#define TR_F_QRU_RD3C "repetidores D-Star 3cm"
/** Spanish text for the form label for the "qru rd70" field or fieldset, rendered on the configuration forms. English: "D-Star 70cm repeaters". */
#define TR_F_QRU_RD70 "repetidores D-Star 70cm"
/** Spanish text for the form label for the "qru rp10" field or fieldset, rendered on the configuration forms. English: "analog 10m repeaters". */
#define TR_F_QRU_RP10 "repetidores analógicos 10m"
/** Spanish text for the form label for the "qru rp13" field or fieldset, rendered on the configuration forms. English: "analog 13cm repeaters". */
#define TR_F_QRU_RP13 "repetidores analógicos 13cm"
/** Spanish text for the form label for the "qru rp23" field or fieldset, rendered on the configuration forms. English: "analog 23cm repeaters". */
#define TR_F_QRU_RP23 "repetidores analógicos 23cm"
/** Spanish text for the form label for the "qru rp2m" field or fieldset, rendered on the configuration forms. English: "analog 2m repeaters". */
#define TR_F_QRU_RP2M "repetidores analógicos 2m"
/** Spanish text for the form label for the "qru rp3c" field or fieldset, rendered on the configuration forms. English: "analog 3cm repeaters". */
#define TR_F_QRU_RP3C "repetidores analógicos 3cm"
/** Spanish text for the form label for the "qru rp6m" field or fieldset, rendered on the configuration forms. English: "analog 6m repeaters". */
#define TR_F_QRU_RP6M "repetidores analógicos 6m"
/** Spanish text for the form label for the "qru rp70" field or fieldset, rendered on the configuration forms. English: "analog 70cm repeaters". */
#define TR_F_QRU_RP70 "repetidores analógicos 70cm"
/** Spanish text for the form label for the "qru rt13" field or fieldset, rendered on the configuration forms. English: "television 13cm repeaters". */
#define TR_F_QRU_RT13 "repetidores de televisión 13cm"
/** Spanish text for the form label for the "qru rt23" field or fieldset, rendered on the configuration forms. English: "television 23cm repeaters". */
#define TR_F_QRU_RT23 "repetidores de televisión 23cm"
/** Spanish text for the form label for the "qru rt3c" field or fieldset, rendered on the configuration forms. English: "television 3cm repeaters". */
#define TR_F_QRU_RT3C "repetidores de televisión 3cm"
/** Spanish text for the form label for the "qru srail" field or fieldset, rendered on the configuration forms. English: "steam railroad". */
#define TR_F_QRU_SRAIL "ferrocarril de vapor"
/** Spanish text for the form label for the "qru stor" field or fieldset, rendered on the configuration forms. English: "Amateur Radio stores". */
#define TR_F_QRU_STOR "tiendas de radioafición"
/** Spanish text for the form label for the "qru t2srv" field or fieldset, rendered on the configuration forms. English: "approx. locations of Tier 2 APRS-IS
 * servers". */
#define TR_F_QRU_T2SRV "ubic. aprox. de servidores APRS-IS Tier 2"
/** Spanish text for the form label for the "qru vete" field or fieldset, rendered on the configuration forms. English: "veterinarians". */
#define TR_F_QRU_VETE "veterinarios"
/** Spanish text for the form label for the "qru wota" field or fieldset, rendered on the configuration forms. English: "Wainwrights On The Air". */
#define TR_F_QRU_WOTA "Wainwrights On The Air"
/** Spanish text for the form label for the "objitem init rate" field or fieldset, rendered on the configuration forms. English: "Initial repeat rate (s)". */
#define TR_F_OBJITEM_INIT_RATE "Intervalo inicial (s)"
/** Spanish text for the form label for the "objitem slow rate" field or fieldset, rendered on the configuration forms. English: "Slow repeat rate (s, 0 = no
 * decay)". */
#define TR_F_OBJITEM_SLOW_RATE "Intervalo lento (s, 0 = sin decaimiento)"
/** Spanish text for the form label for the "objitem decay" field or fieldset, rendered on the configuration forms. English: "Decay ratio (e.g. 2.0, <1 =
 * none)". */
#define TR_F_OBJITEM_DECAY "Razón de decaimiento (ej. 2.0, <1 = ninguna)"
/** Spanish text for the explanatory note shown beside the objitem setting, rendered on the configuration forms. English: "Objects are timestamped (;NAME);
 * Items are never timestamped ()NAME). Unchecking Active...". */
#define TR_NOTE_OBJITEM                                                                                                                                        \
    "Los Objetos llevan marca de tiempo (;NOMBRE); los Ítems nunca la llevan ()NOMBRE). Al desmarcar Activo se envían reportes de eliminación y luego se "     \
    "deshabilita automáticamente. El Alcance limita la transmisión independientemente de las casillas RF/Internet."
/** Spanish text for the form label for the "status beacon" field or fieldset, rendered on the configuration forms. English: "Status Beacon". */
#define TR_F_STATUS_BEACON "Baliza de estado"
/** Spanish text for the form label for the "status interval s 0 off" field or fieldset, rendered on the configuration forms. English: "Status interval (s,
 * 0=off)". */
#define TR_F_STATUS_INTERVAL_S_0_OFF "Intervalo de estado (s, 0=desactivado)"
/** Spanish text for the form label for the "status text" field or fieldset, rendered on the configuration forms. English: "Status text". */
#define TR_F_STATUS_TEXT "Texto de estado"
/** Spanish text for the form label for the "system" field or fieldset, rendered on the configuration forms. English: "System". */
#define TR_F_SYSTEM "Sistema"
/** Spanish text for the form label for the "telemetry" field or fieldset, rendered on the configuration forms. English: "Telemetry". */
#define TR_F_TELEMETRY "Telemetría"
/** Spanish text for the form label for the "beacon" field or fieldset, rendered on the configuration forms. English: "Beacon". */
#define TR_F_BEACON "Baliza"
/** Spanish text for the form label for the "tracker" field or fieldset, rendered on the configuration forms. English: "Tracker". */
#define TR_F_TRACKER "Tracker"
/** Spanish text for the form label for the "tx time slot ms" field or fieldset, rendered on the configuration forms. English: "TX time-slot (ms)". */
#define TR_F_TX_TIME_SLOT_MS "Intervalo de tiempo TX (ms)"
/** Spanish text for the form label for the "upload" field or fieldset, rendered on the configuration forms. English: "Upload". */
#define TR_F_UPLOAD "Subir"
/** Spanish text for the form label for the "username" field or fieldset, rendered on the configuration forms. English: "Username". */
#define TR_F_USERNAME "Usuario"
/** Spanish text for the form label for the "weather" field or fieldset, rendered on the configuration forms. English: "Weather". */
#define TR_F_WEATHER "Clima"
/** Spanish text for the form label for the "weather station" field or fieldset, rendered on the configuration forms. English: "Weather Station". */
#define TR_F_WEATHER_STATION "Estación meteorológica"
/** Spanish text for the form label for the "wireless" field or fieldset, rendered on the configuration forms. English: "Wireless". */
#define TR_F_WIRELESS "Inalámbrico"

/** Spanish text for the form label for the "off" field or fieldset, rendered on the configuration forms. English: "Off". */
#define TR_F_OFF "Apagado"

/** @} */

/**
 * @name page_common.c: dashboard / sysinfo
 * @{
 */
/** Spanish text for the interface string labelling enabled, rendered on the dashboard. English: "enabled". */
#define TR_ENABLED "activado"
/** Spanish text for the dashboard label or value for digi short, rendered on the dashboard. English: "Digi". */
#define TR_DASH_DIGI_SHORT "Digi"
/** Spanish text for the dashboard label or value for wx short, rendered on the dashboard. English: "WX:". */
#define TR_DASH_WX_SHORT "WX:"
/** Spanish text for the dashboard label or value for datetime, rendered on the dashboard. English: "Date/Time:". */
#define TR_DASH_DATETIME "Fecha/Hora:"
/** Spanish text for the dashboard label or value for uptime, rendered on the dashboard. English: "Uptime:". */
#define TR_DASH_UPTIME "Tiempo activo:"
/** Spanish text for the dashboard label or value for free heap, rendered on the dashboard. English: "Free heap:". */
#define TR_DASH_FREE_HEAP "Memoria libre:"
/** Spanish text for the dashboard label or value for littlefs, rendered on the dashboard. English: "LittleFS:". */
#define TR_DASH_LITTLEFS "LittleFS:"
/** Spanish text for the dashboard label or value for sysinfo, rendered on the dashboard. English: "System Info". */
#define TR_DASH_SYSINFO "Información del sistema"
/** Spanish text for the dashboard label or value for igate traffic, rendered on the dashboard. English: "IGate Traffic". */
#define TR_DASH_IGATE_TRAFFIC "Tráfico IGate"
/** Spanish text for the traffic-log column heading or label for pause, rendered on the dashboard. English: "Pause". */
#define TR_TRAFFIC_PAUSE "Pausar"
/** Spanish text for the traffic-log column heading or label for resume, rendered on the dashboard. English: "Resume". */
#define TR_TRAFFIC_RESUME "Reanudar"
/** Spanish text for the traffic-log column heading or label for clear, rendered on the dashboard. English: "Clear". */
#define TR_TRAFFIC_CLEAR "Limpiar"
/** Spanish text for the traffic-log column heading or label for waiting, rendered on the dashboard. English: "Waiting for traffic...". */
#define TR_TRAFFIC_WAITING "Esperando tráfico..."
/** Spanish text for the traffic-log column heading or label for col time, rendered on the dashboard. English: "TIME". */
#define TR_TRAFFIC_COL_TIME "HORA"
/** Spanish text for the traffic-log column heading or label for col type, rendered on the dashboard. English: "TYPE". */
#define TR_TRAFFIC_COL_TYPE "TIPO"
/** Spanish text for the traffic-log column heading or label for col dx, rendered on the dashboard. English: "DX". */
#define TR_TRAFFIC_COL_DX "DX"
/** Spanish text for the traffic-log column heading or label for col packet, rendered on the dashboard. English: "PACKET". */
#define TR_TRAFFIC_COL_PACKET "PAQUETE"
/** Spanish text for the traffic-log column heading or label for col audio, rendered on the dashboard. English: "AUDIO". */
#define TR_TRAFFIC_COL_AUDIO "AUDIO"
/** Spanish text for the column header of the IGate traffic table holding the fields decoded out of the packet. English: "DECODED". */
#define TR_TRAFFIC_COL_DECODED "DECODIFICADO"
/** Spanish text for the system-information row label for chip, rendered on the dashboard. English: "Chip". */
#define TR_SYSINFO_CHIP "Chip"
/** Spanish text for the system-information row label for model, rendered on the dashboard. English: "Model:". */
#define TR_SYSINFO_MODEL "Modelo:"
/** Spanish text for the system-information row label for cores, rendered on the dashboard. English: "Cores:". */
#define TR_SYSINFO_CORES "Núcleos:"
/** Spanish text for the system-information row label for revision, rendered on the dashboard. English: "Revision:". */
#define TR_SYSINFO_REVISION "Revisión:"
/** Spanish text for the system-information row label for cpu freq, rendered on the dashboard. English: "CPU speed:". */
#define TR_SYSINFO_CPU_FREQ "Velocidad de CPU:"
/** Spanish text for the system-information row label for cpu freq set, rendered on the dashboard. English: "Set CPU frequency". */
#define TR_SYSINFO_CPU_FREQ_SET "Establecer frecuencia de CPU"
/** Spanish text for the system-information row label for cpu freq note, rendered on the dashboard. English: "Saved to flash and re-applied automatically on
 * every boot.". */
#define TR_SYSINFO_CPU_FREQ_NOTE "Se guarda en la memoria flash y se vuelve a aplicar automáticamente en cada arranque."
/** Spanish text for the system-information row label for flash size, rendered on the dashboard. English: "Flash size:". */
#define TR_SYSINFO_FLASH_SIZE "Tamaño de flash:"
/** Spanish text for the system-information row label for min free heap, rendered on the dashboard. English: "Min free heap:". */
#define TR_SYSINFO_MIN_FREE_HEAP "Memoria libre mínima:"
/** Spanish text for the dashboard label or value for reboot reason, rendered on the dashboard. English: "Reboot Reason:". */
#define TR_DASH_REBOOT_REASON "Motivo de reinicio:"

/** @} */

/**
 * @name page_common.c
 * @{
 */
/** Spanish text for the dashboard label or value for radio info, rendered on the dashboard. English: "Radio Info". */
#define TR_DASH_RADIO_INFO "Info de Radio"
/** Spanish text for the dashboard label or value for modem, rendered on the dashboard. English: "MODEM". */
#define TR_DASH_MODEM "MÓDEM"
/** Spanish text for the dashboard label or value for fx25, rendered on the dashboard. English: "FX.25". */
#define TR_DASH_FX25 "FX.25"
/** Spanish text for the dashboard label or value for aprs is server, rendered on the dashboard. English: "APRS-IS SERVER". */
#define TR_DASH_APRS_IS_SERVER "SERVIDOR APRS-IS"
/** Spanish text for the dashboard label or value for host, rendered on the dashboard. English: "HOST". */
#define TR_DASH_HOST "HOST"
/** Spanish text for the dashboard label or value for port, rendered on the dashboard. English: "PORT". */
#define TR_DASH_PORT "PUERTO"
/** Spanish text for the dashboard label or value for wifi, rendered on the dashboard. English: "WiFi". */
#define TR_DASH_WIFI "WiFi"
/** Spanish text for the dashboard label or value for mode, rendered on the dashboard. English: "MODE". */
#define TR_DASH_MODE "MODO"
/** Spanish text for the dashboard label or value for ssid, rendered on the dashboard. English: "SSID". */
#define TR_DASH_SSID "SSID"
/** Spanish text for the dashboard label or value for rssi, rendered on the dashboard. English: "RSSI". */
#define TR_DASH_RSSI "RSSI"
/** Spanish text for the dashboard label or value for disconnected, rendered on the dashboard. English: "Disconnect". */
#define TR_DASH_DISCONNECTED "Desconectado"
/** Spanish text for the dashboard label or value for modes enabled, rendered on the dashboard. English: "Modes Enabled". */
#define TR_DASH_MODES_ENABLED "Modos Activos"
/** Spanish text for the dashboard label or value for network status, rendered on the dashboard. English: "Network Status". */
#define TR_DASH_NETWORK_STATUS "Estado de Red"
/** Spanish text for the dashboard label or value for statistics, rendered on the dashboard. English: "STATISTICS". */
#define TR_DASH_STATISTICS "ESTADÍSTICAS"
/** Spanish text for the dashboard label or value for radio rx, rendered on the dashboard. English: "RADIO RX:". */
#define TR_DASH_RADIO_RX "RADIO RX:"
/** Spanish text for the dashboard label or value for packet tx, rendered on the dashboard. English: "RADIO TX:". */
#define TR_DASH_PACKET_TX "RADIO TX:"
/** Spanish text for the dashboard label or value for rf2inet, rendered on the dashboard. English: "RF2INET:". */
#define TR_DASH_RF2INET "RF2INET:"
/** Spanish text for the dashboard label or value for inet2rf, rendered on the dashboard. English: "INET2RF:". */
#define TR_DASH_INET2RF "INET2RF:"
/** Spanish text for the dashboard label or value for igate rx, rendered on the dashboard. English: "IGATE RX:". */
#define TR_DASH_IGATE_RX "IGATE RX:"
/** Spanish text for the dashboard label or value for igate tx, rendered on the dashboard. English: "IGATE TX:". */
#define TR_DASH_IGATE_TX "IGATE TX:"
/** Spanish text for the dashboard label or value for digi stat, rendered on the dashboard. English: "DIGI:". */
#define TR_DASH_DIGI_STAT "DIGI:"
/** Spanish text for the dashboard label or value for drop err, rendered on the dashboard. English: "DROP/ERR:". */
#define TR_DASH_DROP_ERR "DESCARTE/ERR:"
/** Spanish text for the dashboard label or value for drop breakdown, rendered on the dashboard. English: "Drop Breakdown". */
#define TR_DASH_DROP_BREAKDOWN "Detalle de Descartes"
/** Spanish text for the dashboard label or value for tx queue, rendered on the dashboard. English: "RF TX QUEUE:". */
#define TR_DASH_TX_QUEUE "COLA TX RF:"
/** Spanish text for the dashboard label or value for csma forced, rendered on the dashboard. English: "CSMA FORCED (BUSY/PERSIST):". */
#define TR_DASH_CSMA_FORCED "CSMA FORZADO (OCUP./PERSIST.):"
/** Spanish text for the dashboard label or value for tx duty cycle, rendered on the dashboard. English: "TX DUTY CYCLE:". */
#define TR_DASH_TX_DUTY_CYCLE "CICLO DE TRABAJO TX:"
/** Spanish text for the dashboard label or value for lh icon, rendered on the dashboard. English: "ICON". */
#define TR_DASH_LH_ICON "ICONO"

/** @} */

/**
 * @name page_digi.c / page_igate.c / page_tracker.c telemetry notes
 * @{
 */
/** Spanish text for the explanatory note shown beside the tlm digi setting, rendered on the Digipeater, IGate and Tracker pages. English: "Telemetry
 * (EQNS/PARM/UNIT) for Digi beacons is configured on the <a href='/tlm'>Telemet...". */
#define TR_NOTE_TLM_DIGI                                                                                                                                       \
    "La telemetría (EQNS/PARM/UNIT) para las balizas de Digi se configura en la "                                                                              \
    "página <a href='/tlm'>Telemetría</a>."
/** Spanish text for the explanatory note shown beside the tlm igate setting, rendered on the Digipeater, IGate and Tracker pages. English: "Telemetry
 * (EQNS/PARM/UNIT) for IGate beacons is configured on the <a href='/tlm'>Teleme...". */
#define TR_NOTE_TLM_IGATE                                                                                                                                      \
    "La telemetría (EQNS/PARM/UNIT) para las balizas de IGate se configura en la "                                                                             \
    "página <a href='/tlm'>Telemetría</a>."
/** Spanish text for the explanatory note shown beside the tlm tracker setting, rendered on the Digipeater, IGate and Tracker pages. English: "Telemetry
 * (EQNS/PARM/UNIT) for Tracker beacons is configured on the <a href='/tlm'>Tele...". */
#define TR_NOTE_TLM_TRACKER                                                                                                                                    \
    "La telemetría (EQNS/PARM/UNIT) para las balizas de Tracker se configura en la "                                                                           \
    "página <a href='/tlm'>Telemetría</a>."

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
/** Spanish text for the form label for the "snd rcv msg" field or fieldset, rendered on the Send/Receive Message page. English: "Snd/Rcv Msg". */
#define TR_F_SND_RCV_MSG "Env/Rec Msj"
/** Spanish text for the message page label for my station, rendered on the Send/Receive Message page. English: "My Station:". */
#define TR_MSGCHAT_MY_STATION "Mi Estación:"
/** Spanish text for the message page label for disabled note, rendered on the Send/Receive Message page. English: "APRS Messaging is disabled or no station
 * callsign is configured. Enable it and set a ca...". */
#define TR_MSGCHAT_DISABLED_NOTE                                                                                                                               \
    "La Mensajería APRS está deshabilitada o no hay un indicativo configurado. Habilítela y configure un indicativo en la página Message primero."
/** Spanish text for the message page label for loading, rendered on the Send/Receive Message page. English: "Loading messages...". */
#define TR_MSGCHAT_LOADING "Cargando mensajes..."
/** Spanish text for the message page label for empty, rendered on the Send/Receive Message page. English: "No messages yet.". */
#define TR_MSGCHAT_EMPTY "Aún no hay mensajes."
/** Spanish text for the message page label for to, rendered on the Send/Receive Message page. English: "To (callsign):". */
#define TR_MSGCHAT_TO "Para (indicativo):"
/** Spanish text for the message page label for to placeholder, rendered on the Send/Receive Message page. English: "N0CALL-9". */
#define TR_MSGCHAT_TO_PLACEHOLDER "N0CALL-9"
/** Spanish text for the message page label for text, rendered on the Send/Receive Message page. English: "Message:". */
#define TR_MSGCHAT_TEXT "Mensaje:"
/** Spanish text for the message page label for text placeholder, rendered on the Send/Receive Message page. English: "Type a message...". */
#define TR_MSGCHAT_TEXT_PLACEHOLDER "Escriba un mensaje..."
/** Spanish text for the message page label for send, rendered on the Send/Receive Message page. English: "Send". */
#define TR_MSGCHAT_SEND "Enviar"
/** Spanish text for the message page label for you, rendered on the Send/Receive Message page. English: "You". */
#define TR_MSGCHAT_YOU "Tú"
/** Spanish text for the message page label for err empty, rendered on the Send/Receive Message page. English: "Enter a destination callsign and a message.". */
#define TR_MSGCHAT_ERR_EMPTY "Ingrese un indicativo de destino y un mensaje."
/** Spanish text for the message page label for err disabled, rendered on the Send/Receive Message page. English: "APRS Messaging is disabled on the Message
 * page.". */
#define TR_MSGCHAT_ERR_DISABLED "La Mensajería APRS está deshabilitada en la página Message."
/** Spanish text for the message page label for err no mycall, rendered on the Send/Receive Message page. English: "No station callsign configured.". */
#define TR_MSGCHAT_ERR_NO_MYCALL "No hay un indicativo de estación configurado."
/** Spanish text for the message page label for sent ok, rendered on the Send/Receive Message page. English: "Sent.". */
#define TR_MSGCHAT_SENT_OK "Enviado."
/** Spanish text for the message page label for sent fail, rendered on the Send/Receive Message page. English: "Send failed.". */
#define TR_MSGCHAT_SENT_FAIL "Error al enviar."

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
/** Spanish text for the file Storage page label for usage, rendered on the File Storage page. English: "LittleFS usage:". */
#define TR_STORAGE_USAGE "Uso de LittleFS:"
/** Spanish text for the file Storage page label for upload file, rendered on the File Storage page. English: "Upload file". */
#define TR_STORAGE_UPLOAD_FILE "Subir archivo"
/** Spanish text for the file Storage page label for confirm format, rendered on the File Storage page. English: "Erase ALL files and reset config to
 * defaults?". */
#define TR_STORAGE_CONFIRM_FORMAT "¿Borrar TODOS los archivos y restablecer la configuración de fábrica?"
/** Spanish text for the file Storage page label for format btn, rendered on the File Storage page. English: "Format LittleFS". */
#define TR_STORAGE_FORMAT_BTN "Formatear LittleFS"
/** Spanish text for the file Storage page label for size bytes, rendered on the File Storage page. English: "Size (bytes)". */
#define TR_STORAGE_SIZE_BYTES "Tamaño (bytes)"
/** Spanish text for the file Storage page label for actions, rendered on the File Storage page. English: "Actions". */
#define TR_STORAGE_ACTIONS "Acciones"
/** Spanish text for the file Storage page label for download, rendered on the File Storage page. English: "Download". */
#define TR_STORAGE_DOWNLOAD "Descargar"
/** Spanish text for the file Storage page label for confirm delete prefix, rendered on the File Storage page. English: "Delete". */
#define TR_STORAGE_CONFIRM_DELETE_PREFIX "¿Eliminar "
/** Spanish text for the file Storage page label for delete, rendered on the File Storage page. English: "Delete". */
#define TR_STORAGE_DELETE "Eliminar"
/** Spanish text for the file Storage page label for upload ok, rendered on the File Storage page. English: "Uploaded:". */
#define TR_STORAGE_UPLOAD_OK "Subido:"
/** Spanish text for the file Storage page label for upload failed, rendered on the File Storage page. English: "Upload failed. Check the file and make sure
 * there is enough free space.". */
#define TR_STORAGE_UPLOAD_FAILED "La carga falló. Verifique el archivo y que haya espacio libre suficiente."
/** Spanish text for the file Storage page label for no file chosen, rendered on the File Storage page. English: "Choose a file first.". */
#define TR_STORAGE_NO_FILE_CHOSEN "Seleccione primero un archivo."
/** Spanish text for the file Storage page label for back, rendered on the File Storage page. English: "Back". */
#define TR_STORAGE_BACK "Volver"

/** @} */

/**
 * @name page_symbol.c
 * @{
 */
/** Spanish text for the symbol-picker label for house hf, rendered on the symbol picker. English: "House (HF)". */
#define TR_SYM_HOUSE_HF "Casa (HF)"
/** Spanish text for the symbol-picker label for car, rendered on the symbol picker. English: "Car". */
#define TR_SYM_CAR "Automóvil"
/** Spanish text for the symbol-picker label for motorcycle, rendered on the symbol picker. English: "Motorcycle". */
#define TR_SYM_MOTORCYCLE "Motocicleta"
/** Spanish text for the symbol-picker label for bicycle, rendered on the symbol picker. English: "Bicycle". */
#define TR_SYM_BICYCLE "Bicicleta"
/** Spanish text for the symbol-picker label for truck, rendered on the symbol picker. English: "Truck". */
#define TR_SYM_TRUCK "Camión"
/** Spanish text for the symbol-picker label for van, rendered on the symbol picker. English: "Van". */
#define TR_SYM_VAN "Furgoneta"
/** Spanish text for the symbol-picker label for jeep, rendered on the symbol picker. English: "Jeep". */
#define TR_SYM_JEEP "Jeep"
/** Spanish text for the symbol-picker label for fire truck, rendered on the symbol picker. English: "Fire truck". */
#define TR_SYM_FIRE_TRUCK "Camión de bomberos"
/** Spanish text for the symbol-picker label for police, rendered on the symbol picker. English: "Police". */
#define TR_SYM_POLICE "Policía"
/** Spanish text for the symbol-picker label for house, rendered on the symbol picker. English: "House". */
#define TR_SYM_HOUSE "Casa"
/** Spanish text for the symbol-picker label for digipeater, rendered on the symbol picker. English: "Digipeater". */
#define TR_SYM_DIGIPEATER "Digipeater"
/** Spanish text for the symbol-picker label for gateway, rendered on the symbol picker. English: "Gateway". */
#define TR_SYM_GATEWAY "Puerta de enlace"
/** Spanish text for the symbol-picker label for weather station, rendered on the symbol picker. English: "Weather station". */
#define TR_SYM_WEATHER_STATION "Estación meteorológica"
/** Spanish text for the symbol-picker label for balloon, rendered on the symbol picker. English: "Balloon". */
#define TR_SYM_BALLOON "Globo"
/** Spanish text for the symbol-picker label for space shuttle, rendered on the symbol picker. English: "Space shuttle". */
#define TR_SYM_SPACE_SHUTTLE "Transbordador espacial"
/** Spanish text for the symbol-picker label for sailboat, rendered on the symbol picker. English: "Sailboat". */
#define TR_SYM_SAILBOAT "Velero"
/** Spanish text for the symbol-picker label for nws site, rendered on the symbol picker. English: "NWS site". */
#define TR_SYM_NWS_SITE "Sitio NWS"
/** Spanish text for the symbol-picker label for tcp ip, rendered on the symbol picker. English: "TCP/IP". */
#define TR_SYM_TCP_IP "TCP/IP"
/** Spanish text for the symbol-picker label for car alt, rendered on the symbol picker. English: "Car (alternate table)". */
#define TR_SYM_CAR_ALT "Automóvil (tabla alterna)"
/** Spanish text for the symbol-picker label for wx station alt, rendered on the symbol picker. English: "WX station (alternate table)". */
#define TR_SYM_WX_STATION_ALT "Estación WX (tabla alterna)"
/** Spanish text for the symbol-picker label for intro, rendered on the symbol picker. English: "Quick reference for common APRS symbol codes. Each service page
 * (IGate / Digi / Tracker...". */
#define TR_SYM_INTRO                                                                                                                                           \
    "Referencia rápida de los códigos de símbolo APRS más comunes. Cada página de "                                                                            \
    "servicio (IGate / Digi / Tracker) tiene su propio campo de símbolo de texto libre "                                                                       \
    "\u2014 copie el código de 2 caracteres desde aquí a ese campo."
/** Spanish text for the symbol-picker label for code, rendered on the symbol picker. English: "Code". */
#define TR_SYM_CODE "Código"
/** Spanish text for the symbol-picker label for meaning, rendered on the symbol picker. English: "Meaning". */
#define TR_SYM_MEANING "Significado"
/** Spanish text for the symbol-picker label for currently configured, rendered on the symbol picker. English: "Currently configured symbols". */
#define TR_SYM_CURRENTLY_CONFIGURED "Símbolos actualmente configurados"

/** @} */

/**
 * @name page_system.c
 * @{
 */
/** Spanish text for the system page label for web admin login, rendered on the System page. English: "Web Admin Login". */
#define TR_SYS_WEB_ADMIN_LOGIN "Acceso al administrador web"
/** System page note clarifying that a blank username only disables the password prompt, rendered on the System page. */
#define TR_SYS_WEB_ADMIN_LOGIN_NOTE                                                                                                                            \
    "Dejar el usuario en blanco desactiva solo el aviso de contraseña. Las solicitudes deben seguir siendo del mismo origen para cualquier cambio "            \
    "realizado a través de este panel de administración."
/** Spanish text for the system page label for time, rendered on the System page. English: "Time". */
#define TR_SYS_TIME "Hora"
/** Spanish text for the system page label for sync ntp, rendered on the System page. English: "Sync time via NTP". */
#define TR_SYS_SYNC_NTP "Sincronizar hora vía NTP"
/** Spanish text for the system page label for ntp host, rendered on the System page. English: "NTP host (primary)". */
#define TR_SYS_NTP_HOST "Host NTP (primario)"
/** Spanish text for the system page label for ntp host2, rendered on the System page. English: "NTP host (fallback 2)". */
#define TR_SYS_NTP_HOST2 "Host NTP (alternativo 2)"
/** Spanish text for the system page label for ntp host3, rendered on the System page. English: "NTP host (fallback 3)". */
#define TR_SYS_NTP_HOST3 "Host NTP (alternativo 3)"
/** Spanish text for the system page label for ntp resync, rendered on the System page. English: "NTP resync interval (s, min 30)". */
#define TR_SYS_NTP_RESYNC "Intervalo de resincronización NTP (s, mín 30)"
/** Spanish text for the system page label for timezone, rendered on the System page. English: "Time zone (dashboard display only)". */
#define TR_SYS_TIMEZONE "Zona horaria (solo para el panel)"
/** Spanish text for the system page label for digi path aliases, rendered on the System page. English: "Digipeater Path Aliases". */
#define TR_SYS_DIGI_PATH_ALIASES "Alias de ruta del Digipeater"
/** Spanish text for the system page label for path 1, rendered on the System page. English: "Path 1". */
#define TR_SYS_PATH_1 "Ruta 1"
/** Spanish text for the system page label for path 2, rendered on the System page. English: "Path 2". */
#define TR_SYS_PATH_2 "Ruta 2"
/** Spanish text for the system page label for path 3, rendered on the System page. English: "Path 3". */
#define TR_SYS_PATH_3 "Ruta 3"
/** Spanish text for the system page label for path 4, rendered on the System page. English: "Path 4". */
#define TR_SYS_PATH_4 "Ruta 4"
/** Spanish text for the system page label for confirm factory reset, rendered on the System page. English: "Reset ALL settings to factory defaults?". */
#define TR_SYS_CONFIRM_FACTORY_RESET "¿Restablecer TODA la configuración a los valores de fábrica?"
/** Spanish text for the system page label for factory reset, rendered on the System page. English: "Factory Reset". */
#define TR_SYS_FACTORY_RESET "Restablecer a fábrica"

/** @} */

/**
 * @name page_tlm.c
 * @{
 */
/** Spanish text for the telemetry configurator label for avg, rendered on the Telemetry page. English: "Avg". */
#define TR_TLM_AVG "Prom"
/** Spanish text for the telemetry configurator label for bit, rendered on the Telemetry page. English: "Bit". */
#define TR_TLM_BIT "Bit"
/** @} */

/**
 * @name page_tlm.c: telemetry configurator
 * @{
 */
/** Spanish text for the telemetry configurator label for enable telemetry, rendered on the Telemetry page. English: "Enable Telemetry". */
#define TR_TLM_ENABLE_TELEMETRY "Habilitar telemetría"
/** Spanish text for the telemetry configurator label for report params, rendered on the Telemetry page. English: "Report Parameters". */
#define TR_TLM_REPORT_PARAMS "Parámetros del reporte"
/** Spanish text for the telemetry configurator label for path digis, rendered on the Telemetry page. English: "Path (digipeaters)". */
#define TR_TLM_PATH_DIGIS "Ruta (digipeaters)"
/** Spanish text for the telemetry configurator label for destination, rendered on the Telemetry page. English: "Destination". */
#define TR_TLM_DESTINATION "Destino"
/** Spanish text for the telemetry configurator label for auto inc seq, rendered on the Telemetry page. English: "Auto-increment sequence". */
#define TR_TLM_AUTO_INC_SEQ "Autoincrementar secuencia"
/** Spanish text for the telemetry configurator label for analog field width, rendered on the Telemetry page. English: "Analog field width". */
#define TR_TLM_ANALOG_FIELD_WIDTH "Ancho de campo analógico"
/** Spanish text for the telemetry configurator label for fieldw 3digit, rendered on the Telemetry page. English: "3-digit zero-padded (000-255, strict)". */
#define TR_TLM_FIELDW_3DIGIT "3 dígitos con ceros (000-255, estricto)"
/** Spanish text for the telemetry configurator label for fieldw auto, rendered on the Telemetry page. English: "Minimal / as-needed (integers or decimals)". */
#define TR_TLM_FIELDW_AUTO "Mínimo / según necesidad (enteros o decimales)"
/** Spanish text for the telemetry configurator label for omit trailing, rendered on the Telemetry page. English: "Omit unused trailing channels". */
#define TR_TLM_OMIT_TRAILING "Omitir canales finales sin usar"
/** Spanish text for the telemetry configurator label for trail comment, rendered on the Telemetry page. English: "Trailing comment (optional, after bits)". */
#define TR_TLM_TRAIL_COMMENT "Comentario final (opcional, tras los bits)"
/** Spanish text for the telemetry configurator label for comment tlm, rendered on the Telemetry page. English: "Also carry telemetry in position comment
 * (APRS 1.2, |ss..|)". */
#define TR_TLM_COMMENT_TLM "Incluir también telemetría en el comentario de posición (APRS 1.2, |ss..|)"
/** Spanish text for the telemetry configurator label for analog count, rendered on the Telemetry page. English: "Analog channels sent". */
#define TR_TLM_ANALOG_COUNT "Canales analógicos enviados"
/** Spanish text for the telemetry configurator label for digital count, rendered on the Telemetry page. English: "Digital bits sent". */
#define TR_TLM_DIGITAL_COUNT "Bits digitales enviados"
/** Spanish text for the telemetry configurator label for def messages, rendered on the Telemetry page. English: "Definition Messages". */
#define TR_TLM_DEF_MESSAGES "Mensajes de definición"
/** Spanish text for the telemetry configurator label for gen parm, rendered on the Telemetry page. English: "PARM - channel & bit names". */
#define TR_TLM_GEN_PARM "PARM - nombres de canal y bits"
/** Spanish text for the telemetry configurator label for gen unit, rendered on the Telemetry page. English: "UNIT - units / bit-state labels". */
#define TR_TLM_GEN_UNIT "UNIT - unidades / etiquetas de estado de bit"
/** Spanish text for the telemetry configurator label for gen eqns, rendered on the Telemetry page. English: "EQNS - scaling coefficients (A,B,C)". */
#define TR_TLM_GEN_EQNS "EQNS - coeficientes de escala (A,B,C)"
/** Spanish text for the telemetry configurator label for gen bits, rendered on the Telemetry page. English: "BITS - bit sense + name". */
#define TR_TLM_GEN_BITS "BITS - sentido de bit + nombre"
/** Spanish text for the telemetry configurator label for analog legend, rendered on the Telemetry page. English: "Analog Channels (A1-A5)". */
#define TR_TLM_ANALOG_LEGEND "Canales analógicos (A1-A5)"
/** Spanish text for the telemetry configurator label for digital legend, rendered on the Telemetry page. English: "Digital Channels (B1-B8)". */
#define TR_TLM_DIGITAL_LEGEND "Canales digitales (B1-B8)"
/** Spanish text for the telemetry configurator label for unit, rendered on the Telemetry page. English: "Unit". */
#define TR_TLM_UNIT "Unidad"
/** Spanish text for the telemetry configurator label for source, rendered on the Telemetry page. English: "Source". */
#define TR_TLM_SOURCE "Fuente"
/** Spanish text for the telemetry configurator label for rf, rendered on the Telemetry page. English: "RF". */
#define TR_TLM_RF "RF"
/** Spanish text for the telemetry configurator label for raw min, rendered on the Telemetry page. English: "Raw min". */
#define TR_TLM_RAW_MIN "Bruto mín"
/** Spanish text for the telemetry configurator label for raw max, rendered on the Telemetry page. English: "Raw max". */
#define TR_TLM_RAW_MAX "Bruto máx"
/** Spanish text for the telemetry configurator label for coef a, rendered on the Telemetry page. English: "A (quadratic)". */
#define TR_TLM_COEF_A "A (cuadrático)"
/** Spanish text for the telemetry configurator label for coef b, rendered on the Telemetry page. English: "B (linear / slope)". */
#define TR_TLM_COEF_B "B (lineal / pendiente)"
/** Spanish text for the telemetry configurator label for coef c, rendered on the Telemetry page. English: "C (offset)". */
#define TR_TLM_COEF_C "C (desplazamiento)"
/** Spanish text for the telemetry configurator label for decimals, rendered on the Telemetry page. English: "Displayed decimals". */
#define TR_TLM_DECIMALS "Decimales mostrados"
/** Spanish text for the telemetry configurator label for on state, rendered on the Telemetry page. English: "On-state means". */
#define TR_TLM_ON_STATE "Significado de estado activo"
/** Spanish text for the telemetry configurator label for sense, rendered on the Telemetry page. English: "Sense". */
#define TR_TLM_SENSE "Sentido"
/** Spanish text for the telemetry configurator label for label, rendered on the Telemetry page. English: "Label". */
#define TR_TLM_LABEL "Etiqueta"
/** Spanish text for the telemetry configurator label for calib wizard, rendered on the Telemetry page. English: "2-point calibration wizard". */
#define TR_TLM_CALIB_WIZARD "Asistente de calibracion de 2 puntos"
/** Spanish text for the telemetry configurator label for calib prompt x1, rendered on the Telemetry page. English: "Raw reading #1 (x1):". */
#define TR_TLM_CALIB_PROMPT_X1 "Lectura cruda #1 (x1):"
/** Spanish text for the telemetry configurator label for calib prompt y1, rendered on the Telemetry page. English: "Known real-world value at x1:". */
#define TR_TLM_CALIB_PROMPT_Y1 "Valor real conocido en x1:"
/** Spanish text for the telemetry configurator label for calib prompt x2, rendered on the Telemetry page. English: "Raw reading #2 (x2):". */
#define TR_TLM_CALIB_PROMPT_X2 "Lectura cruda #2 (x2):"
/** Spanish text for the telemetry configurator label for calib prompt y2, rendered on the Telemetry page. English: "Known real-world value at x2:". */
#define TR_TLM_CALIB_PROMPT_Y2 "Valor real conocido en x2:"
/** Spanish text for the telemetry configurator label for calib same x, rendered on the Telemetry page. English: "x1 and x2 must differ.". */
#define TR_TLM_CALIB_SAME_X "x1 y x2 deben ser diferentes."
/** Spanish text for the telemetry configurator label for calib cancelled, rendered on the Telemetry page. English: "Calibration cancelled: enter numeric
 * values.". */
#define TR_TLM_CALIB_CANCELLED "Calibracion cancelada: ingrese valores numericos."

/** @} */

/**
 * @name page_radio.c
 * @{
 */
/** Spanish text for the radiomodem page label for audio hw title, rendered on the Radiomodem page. English: "Audio hardware (compile-time)". */
#define TR_RADIO_AUDIO_HW_TITLE "Hardware de audio (en tiempo de compilación)"
/** Spanish text for the radiomodem page label for audio hw info, rendered on the Radiomodem page. English: "<br>DAC out: GPIO%d<br>ADC in: GPIO%d<br>PTT pin:
 * %s<br>PTT active-high: %s<br>ADC atte...". */
#define TR_RADIO_AUDIO_HW_INFO                                                                                                                                 \
    "<br>DAC salida: GPIO%d<br>ADC entrada: GPIO%d<br>Pin PTT: %s<br>PTT activo en alto: %s<br>Atenuación ADC: %d<br>ADC: %d Hz<br>DAC: %d Hz"
/** Spanish text: Radiomodem page label for audio hw note, rendered on the Radiomodem page. */
#define TR_RADIO_AUDIO_HW_NOTE ""

/** @} */

/**
 * @name page_wireless.c
 * @{
 */
/** Spanish text for the wireless page label for mode legend, rendered on the Wireless page. English: "WiFi Mode". */
#define TR_WIFI_MODE_LEGEND "Modo WiFi"
/** Spanish text for the wireless page label for station, rendered on the Wireless page. English: "Station (STA)". */
#define TR_WIFI_STATION "Estación (STA)"
/** Spanish text for the wireless page label for access point, rendered on the Wireless page. English: "Access Point (AP)". */
#define TR_WIFI_ACCESS_POINT "Punto de acceso (AP)"
/** Spanish text for the wireless page label for ap sta, rendered on the Wireless page. English: "AP + STA". */
#define TR_WIFI_AP_STA "AP + STA"
/** Spanish text for the wireless page label for tx power, rendered on the Wireless page. English: "TX Power (0-20 dBm)". */
#define TR_WIFI_TX_POWER "Potencia TX (0-20 dBm)"
/** Spanish text for the wireless page label for ap ssid, rendered on the Wireless page. English: "AP SSID". */
#define TR_WIFI_AP_SSID "SSID del AP"
/** Spanish text for the wireless page label for ap password, rendered on the Wireless page. English: "AP Password". */
#define TR_WIFI_AP_PASSWORD "Contraseña del AP"
/** Spanish text for the wireless page label for ap channel, rendered on the Wireless page. English: "AP Channel". */
#define TR_WIFI_AP_CHANNEL "Canal del AP"
/** Spanish text for the wireless page label for client legend, rendered on the Wireless page. English: "WiFi Client #%d". */
#define TR_WIFI_CLIENT_LEGEND "Cliente WiFi n.º %d"
/** Spanish text for the caption of the "wifi scan" button, rendered on the Wireless page. English: "WIFI SCAN". */
#define TR_BTN_WIFI_SCAN "ESCANEAR WIFI"
/** Spanish text for the wireless page label for ssid placeholder, rendered on the Wireless page. English: "Network name (type it, or use WiFi Scan)". */
#define TR_WIFI_SSID_PLACEHOLDER "Nombre de la red (escríbalo o use Buscar WiFi)"
/** Spanish text for the wireless page label for sta needs ssid, rendered on the Wireless page. English: "Saved, but this will NOT connect: Mode selects a
 * station, yet no WiFi Client block has...". */
#define TR_WIFI_STA_NEEDS_SSID                                                                                                                                 \
    "Guardado, pero NO se conectará: el Modo selecciona estación, pero ningún bloque Cliente WiFi tiene 'Habilitar' marcado y un SSID cargado. Corríjalo y "   \
    "guarde de nuevo."
/** Spanish text for the wireless page label for scanning, rendered on the Wireless page. English: "Scanning...". */
#define TR_WIFI_SCANNING "Escaneando..."
/** Spanish text for the wireless page label for scan failed, rendered on the Wireless page. English: "Scan failed". */
#define TR_WIFI_SCAN_FAILED "Error al escanear"

/** @} */

/**
 * @name page_wx.c
 * @{
 */
/** Spanish text for the weather page label for wind speed, rendered on the Weather page. English: "Wind Speed". */
#define TR_WX_WIND_SPEED "Velocidad del viento"
/** Spanish text for the weather page label for wind gust, rendered on the Weather page. English: "Wind Gust". */
#define TR_WX_WIND_GUST "Ráfaga de viento"
/** Spanish text for the weather page label for wind direction, rendered on the Weather page. English: "Wind Direction". */
#define TR_WX_WIND_DIRECTION "Dirección del viento"
/** Spanish text for the weather page label for temperature, rendered on the Weather page. English: "Temperature". */
#define TR_WX_TEMPERATURE "Temperatura"
/** Spanish text for the weather page label for rain 1h, rendered on the Weather page. English: "Rain 1h". */
#define TR_WX_RAIN_1H "Lluvia 1h"
/** Spanish text for the weather page label for rain 24h, rendered on the Weather page. English: "Rain 24h". */
#define TR_WX_RAIN_24H "Lluvia 24h"
/** Spanish text for the weather page label for rain midnight, rendered on the Weather page. English: "Rain since midnight". */
#define TR_WX_RAIN_MIDNIGHT "Lluvia desde medianoche"
/** Spanish text for the weather page label for humidity, rendered on the Weather page. English: "Humidity". */
#define TR_WX_HUMIDITY "Humedad"
/** Spanish text for the weather page label for pressure, rendered on the Weather page. English: "Pressure". */
#define TR_WX_PRESSURE "Presión"
/** Spanish text for the weather page label for luminosity, rendered on the Weather page. English: "Luminosity". */
#define TR_WX_LUMINOSITY "Luminosidad"
/** Spanish text for the weather page label for snow, rendered on the Weather page. English: "Snow". */
#define TR_WX_SNOW "Nieve"
/** Spanish text for the weather page label for flood ft, rendered on the Weather page. English: "Flood Height (ft)". */
#define TR_WX_FLOOD_FT "Nivel de crecida (ft)"
/** Spanish text for the weather page label for flood m, rendered on the Weather page. English: "Flood Height". */
#define TR_WX_FLOOD_M "Nivel de crecida"
/** Weather page label for the raw rain counter, rendered on the Weather page. */
#define TR_WX_RAIN_RAW "Contador de lluvia crudo"
/** Spanish text for the weather page label for field, rendered on the Weather page. English: "WX Field". */
#define TR_WX_FIELD "Campo WX"
/** Spanish text for the weather page label for channel, rendered on the Weather page. English: "Channel". */
#define TR_WX_CHANNEL "Canal"
/** Spanish text for the weather page label for channel none, rendered on the Weather page. English: "(none)". */
#define TR_WX_CHANNEL_NONE "(ninguno)"
/** Spanish text for the weather page label for value, rendered on the Weather page. English: "Value". */
#define TR_WX_VALUE "Valor"

/** @} */

/**
 * @name IGATE page additions (station symbol, path preset, timestamp, PHG, filters)
 * @{
 */
/** Spanish text for the form label for the "station symbol" field or fieldset, rendered on the IGate page. English: "Station Symbol". */
#define TR_F_STATION_SYMBOL "Símbolo de Estación"
/** Spanish text for the form label for the "symbol table" field or fieldset, rendered on the IGate page. English: "Table". */
#define TR_F_SYMBOL_TABLE "Tabla"
/** Spanish text for the form label for the "symbol code" field or fieldset, rendered on the IGate page. English: "Symbol". */
#define TR_F_SYMBOL_CODE "Símbolo"
/** Spanish text for the caption of the "pick symbol" button, rendered on the IGate page. English: "...". */
#define TR_BTN_PICK_SYMBOL "..."
/** Spanish text for the symbol-picker label for pick hint, rendered on the IGate page. English: "Click icon for select symbol". */
#define TR_SYM_PICK_HINT "Haga clic en el icono para elegir el símbolo"
/** Spanish text for the form label for the "path" field or fieldset, rendered on the IGate page. English: "PATH". */
#define TR_F_PATH "RUTA"
/** Spanish text for the digipeat path preset label for direct, rendered on the IGate page. English: "Direct (no path)". */
#define TR_PATH_DIRECT "Directo (sin ruta)"
/** Spanish text for the digipeat path preset label for custom unset, rendered on the IGate page. English: "(not set)". */
#define TR_PATH_CUSTOM_UNSET "(no configurado)"
/** Spanish text for the digipeat path preset label for direct hint, rendered on the IGate page. English: "no digipeater path - only stations that hear you
 * directly will receive it". */
#define TR_PATH_DIRECT_HINT "sin ruta de digipeater - solo lo recibirán estaciones que lo escuchen directamente"
/** Spanish text for the digipeat path preset label for hop hint, rendered on the IGate page. English: "hop(s) via digipeater, encoded as an SSID suffix (short
 * WIDEn-N form)". */
#define TR_PATH_HOP_HINT "salto(s) vía digipeater, codificado como sufijo SSID (forma corta WIDEn-N)"
/** Spanish text for the digipeat path preset label for custom hint, rendered on the IGate page. English: "custom digipeater path configured on the System
 * page". */
#define TR_PATH_CUSTOM_HINT "ruta de digipeater personalizada configurada en la página Sistema"
/** Spanish text for the form label for the "time stamp" field or fieldset, rendered on the IGate page. English: "Time Stamp". */
#define TR_F_TIME_STAMP "Marca de Tiempo"
/** Spanish text for the form label for the "tx channel" field or fieldset, rendered on the IGate page. English: "TX Channel". */
#define TR_F_TX_CHANNEL "Canal TX"
/** Spanish text for the form label for the "phg section" field or fieldset, rendered on the IGate page. English: "PHG". */
#define TR_F_PHG_SECTION "PHG"
/** Spanish text for the form label for the "enable phg" field or fieldset, rendered on the IGate page. English: "Enable PHG". */
#define TR_F_ENABLE_PHG "Habilitar PHG"
/** Spanish text for the form label for the "radio tx power" field or fieldset, rendered on the IGate and Digipeater pages. English: "Radio TX Power". */
#define TR_F_RADIO_TX_POWER "Potencia TX de Radio"
/** Spanish text for the form label for the "antenna gain" field or fieldset, rendered on the IGate and Digipeater pages. English: "Antenna Gain". */
#define TR_F_ANTENNA_GAIN "Ganancia de Antena"
/** Spanish text for the form label for the "height m" field or fieldset, rendered on the IGate and Digipeater pages. English: "Height (m)". */
#define TR_F_HEIGHT_M "Altura (m)"
/** Spanish text for the form label for the "antenna direction" field or fieldset, rendered on the IGate and Digipeater pages. English: "Antenna/Direction". */
#define TR_F_ANTENNA_DIRECTION "Antena/Dirección"
/** Spanish text for the form label for the "phg text" field or fieldset, rendered on the IGate and Digipeater pages. English: "PHG Text". */
#define TR_F_PHG_TEXT "Texto PHG"
/** Spanish text for the form label for the "ext section" field or fieldset, rendered on the IGate and Digipeater pages. English: "Data Extension". */
#define TR_F_EXT_SECTION "Extension de datos"
/** Spanish text for the form label for the "enable ext" field or fieldset, rendered on the IGate and Digipeater pages. English: "Enable data extension". */
#define TR_F_ENABLE_EXT "Habilitar extension de datos"
/** Spanish text for the form label for the "ext type" field or fieldset, rendered on the IGate and Digipeater pages. English: "Extension type". */
#define TR_F_EXT_TYPE "Tipo de extension"
/** Spanish text for the data-extension label for phg, rendered on the IGate and Digipeater pages. English: "PHG - power/height/gain/directivity". */
#define TR_EXT_PHG "PHG - potencia/altura/ganancia/directividad"
/** Spanish text for the data-extension label for rng, rendered on the IGate and Digipeater pages. English: "RNG - pre-calculated radio range". */
#define TR_EXT_RNG "RNG - alcance de radio precalculado"
/** Spanish text for the data-extension label for dfs, rendered on the IGate and Digipeater pages. English: "DFS - omni-DF signal strength". */
#define TR_EXT_DFS "DFS - intensidad de senal omni-DF"
/** Spanish text for the data-extension label for df, rendered on the IGate and Digipeater pages. English: "DF - bearing and NRQ report". */
#define TR_EXT_DF "DF - reporte de rumbo y NRQ"
/** Spanish text for the explanatory note shown in the Data Extension fieldset when DF is selected but the station symbol is not the DF symbol, rendered on
 * the IGate and Digipeater pages. English: "The DF report is only meaningful with the DF symbol (table '/', code '\\'), so with the symbol set above it is not
 * transmitted.". */
#define TR_NOTE_EXT_DF_SYMBOL                                                                                                                                  \
    "El reporte DF solo tiene sentido con el símbolo DF (tabla '/', código '\\'), "                                                                            \
    "así que con el símbolo configurado arriba no se transmite."
/** Spanish text for the form label for the "ext range mi" field or fieldset, rendered on the IGate and Digipeater pages. English: "Radio range (miles)". */
#define TR_F_EXT_RANGE_MI "Alcance de radio (millas)"
/** Spanish text for the form label for the "ext dfs strength" field or fieldset, rendered on the IGate and Digipeater pages. English: "Signal strength
 * (S-points, 0 = not heard)". */
#define TR_F_EXT_DFS_STRENGTH "Intensidad de senal (puntos S, 0 = no se oye)"
/** Spanish text for the form label for the "ext df bearing" field or fieldset, rendered on the IGate and Digipeater pages. English: "Signal bearing (degrees)".
 */
#define TR_F_EXT_DF_BEARING "Rumbo de la senal (grados)"
/** Spanish text for the form label for the "ext df nrq n" field or fieldset, rendered on the IGate and Digipeater pages. English: "Hits per period (N, 0 = NRQ
 * not meaningful)". */
#define TR_F_EXT_DF_NRQ_N "Deteccion por periodo (N, 0 = NRQ sin significado)"
/** Spanish text for the form label for the "ext df nrq r" field or fieldset, rendered on the IGate and Digipeater pages. English: "Range code (R, range = 2^R
 * miles)". */
#define TR_F_EXT_DF_NRQ_R "Codigo de alcance (R, alcance = 2^R millas)"
/** Spanish text for the form label for the "ext df nrq q" field or fieldset, rendered on the IGate and Digipeater pages. English: "Bearing accuracy (Q, 9 =
 * best)". */
#define TR_F_EXT_DF_NRQ_Q "Precision del rumbo (Q, 9 = mejor)"
/** Spanish text for the form label for the "pos ambiguity" field or fieldset, rendered on the IGate page. English: "Position ambiguity". */
#define TR_F_POS_AMBIGUITY "Ambiguedad de posicion"
/** Spanish text for the position-ambiguity selector entry: none, rendered on the IGate page. English: "Full precision". */
#define TR_AMB_NONE "Precision total"
/** Spanish text for the position-ambiguity selector entry: tenth, rendered on the IGate page. English: "Nearest 1/10 minute". */
#define TR_AMB_TENTH "Al 1/10 de minuto"
/** Spanish text for the position-ambiguity selector entry: minute, rendered on the IGate page. English: "Nearest minute". */
#define TR_AMB_MINUTE "Al minuto"
/** Spanish text for the position-ambiguity selector entry: ten minutes, rendered on the IGate page. English: "Nearest 10 minutes". */
#define TR_AMB_TEN_MINUTES "A los 10 minutos"
/** Spanish text for the position-ambiguity selector entry: degree, rendered on the IGate page. English: "Nearest degree". */
#define TR_AMB_DEGREE "Al grado"
/** Spanish text for the form label for the "status grid" field or fieldset, rendered on the IGate page. English: "Maidenhead locator in status reports". */
#define TR_F_STATUS_GRID "Localizador Maidenhead en los reportes de estado"
/** Spanish text for the form label for the "status timestamp" field or fieldset, rendered on the IGate page. English: "Zulu timestamp in status reports". */
#define TR_F_STATUS_TIMESTAMP "Marca de tiempo zulu en los reportes de estado"
/** Form label for the "status beam" field or fieldset, rendered on the IGate page. */
#define TR_F_STATUS_BEAM "Rumbo de antena en los reportes de estado (grados)"
/** Form label for the "status erp" field or fieldset, rendered on the IGate page. */
#define TR_F_STATUS_ERP "PRE en los reportes de estado (W)"
/** Spanish text for the form label for the "pos dao" field or fieldset, rendered on the IGate page. English: "DAO precision extension in position reports". */
#define TR_F_POS_DAO "Extensión de precisión DAO en los reportes de posición"
/** Spanish text for the form label for the "no archive" field or fieldset, rendered on the Station page. English: "Request APRS-IS not to archive my packets
 * (!x!)". */
#define TR_F_NO_ARCHIVE "Solicitar a APRS-IS que no archive mis paquetes (!x!)"
/** Spanish text for the compass direction abbreviation: omni, rendered on the IGate and Digipeater pages. English: "Omni". */
#define TR_DIR_OMNI "Omni"
/** Spanish text for the compass direction abbreviation: n, rendered on the IGate and Digipeater pages. English: "N". */
#define TR_DIR_N "N"
/** Spanish text for the compass direction abbreviation: ne, rendered on the IGate and Digipeater pages. English: "NE". */
#define TR_DIR_NE "NE"
/** Spanish text for the compass direction abbreviation: e, rendered on the IGate and Digipeater pages. English: "E". */
#define TR_DIR_E "E"
/** Spanish text for the compass direction abbreviation: se, rendered on the IGate and Digipeater pages. English: "SE". */
#define TR_DIR_SE "SE"
/** Spanish text for the compass direction abbreviation: s, rendered on the IGate and Digipeater pages. English: "S". */
#define TR_DIR_S "S"
/** Spanish text for the compass direction abbreviation: sw, rendered on the IGate and Digipeater pages. English: "SW". */
#define TR_DIR_SW "SO"
/** Spanish text for the compass direction abbreviation: w, rendered on the IGate and Digipeater pages. English: "W". */
#define TR_DIR_W "O"
/** Spanish text for the compass direction abbreviation: nw, rendered on the IGate and Digipeater pages. English: "NW". */
#define TR_DIR_NW "NO"
/** Spanish text for the form label for the "igate filter" field or fieldset, rendered on the IGate page. English: "IGate Filter". */
#define TR_F_IGATE_FILTER "Filtro IGate"
/** Spanish text for the form label for the "filter rf2inet" field or fieldset, rendered on the IGate page. English: "Filter RF to Internet". */
#define TR_F_FILTER_RF2INET "Filtro RF a Internet"
/** Spanish text for the form label for the "filter inet2rf" field or fieldset, rendered on the IGate page. English: "Filter Internet to RF". */
#define TR_F_FILTER_INET2RF "Filtro Internet a RF"
/** Spanish text for the aPRS-IS filter editor label for message, rendered on the IGate page. English: "Message". */
#define TR_FILT_MESSAGE "Mensaje"
/** Spanish text for the aPRS-IS filter editor label for status, rendered on the IGate page. English: "Status". */
#define TR_FILT_STATUS "Estado"
/** Spanish text for the aPRS-IS filter editor label for telemetry, rendered on the IGate page. English: "Telemetry". */
#define TR_FILT_TELEMETRY "Telemetría"
/** Spanish text for the aPRS-IS filter editor label for weather, rendered on the IGate page. English: "Weather". */
#define TR_FILT_WEATHER "Clima"
/** Spanish text for the aPRS-IS filter editor label for object, rendered on the IGate page. English: "Object". */
#define TR_FILT_OBJECT "Objeto"
/** Spanish text for the aPRS-IS filter editor label for item, rendered on the IGate page. English: "Item". */
#define TR_FILT_ITEM "Ítem"
/** Spanish text for the aPRS-IS filter editor label for buoy, rendered on the IGate page. English: "Buoy". */
#define TR_FILT_BUOY "Boya"
/** Spanish text for the aPRS-IS filter editor label for position, rendered on the IGate page. English: "Position". */
#define TR_FILT_POSITION "Posición"
/** Spanish text for the APRS-IS filter editor label for the payload kinds that share one bit (capacidades, formatos definidos por el usuario, radiogoniometría,
 * balizas de localizador, elemento de mapa), rendered on the IGate page. English: "Other". */
#define TR_FILT_OTHER "Otros"

/** Spanish text for the form label for the "callsign filter" field or fieldset, rendered on the IGate page. English: "Callsign Filter". */
#define TR_F_CALLSIGN_FILTER "Filtro de Indicativos"
/** Spanish text for the form label for the "budlist mode rf2inet" field or fieldset, rendered on the IGate page. English: "RF to Internet Mode". */
#define TR_F_BUDLIST_MODE_RF2INET "Modo RF a Internet"
/** Spanish text for the form label for the "budlist mode inet2rf" field or fieldset, rendered on the IGate page. English: "Internet to RF Mode". */
#define TR_F_BUDLIST_MODE_INET2RF "Modo Internet a RF"
/** Spanish text for the buddy-list editor label for off, rendered on the IGate page. English: "Off". */
#define TR_BUDLIST_OFF "Desactivado"
/** Spanish text for the buddy-list editor label for whitelist, rendered on the IGate page. English: "Whitelist". */
#define TR_BUDLIST_WHITELIST "Lista Blanca"
/** Spanish text for the buddy-list editor label for blacklist, rendered on the IGate page. English: "Blacklist". */
#define TR_BUDLIST_BLACKLIST "Lista Negra"
/** Spanish text for the form label for the "budlist call" field or fieldset, rendered on the IGate page. English: "Callsign". */
#define TR_F_BUDLIST_CALL "Indicativo"
/** Spanish text for the explanatory note shown beside the budlist setting, rendered on the IGate page. English: "Shared callsign list, up to 8 entries.
 * Whitelist: only listed calls pass. Blacklist: li...". */
#define TR_NOTE_BUDLIST                                                                                                                                        \
    "Lista de indicativos compartida, hasta 8 entradas. Lista Blanca: solo pasan los indicativos listados. Lista Negra: los indicativos listados se bloquean."

/** Spanish text for the form label for the "range filter en" field or fieldset, rendered on the IGate page. English: "Enable range filter". */
#define TR_F_RANGE_FILTER_EN "Activar filtro de distancia"
/** Spanish text for the form label for the "range km" field or fieldset, rendered on the IGate page. English: "Max distance (km, 0 = unlimited)". */
#define TR_F_RANGE_KM "Distancia máxima (km, 0 = sin límite)"
/** Spanish text for the form label for the "prefix filter en" field or fieldset, rendered on the IGate page. English: "Enable callsign-prefix filter". */
#define TR_F_PREFIX_FILTER_EN "Activar filtro de prefijo de indicativo"
/** Spanish text for the form label for the "prefixes" field or fieldset, rendered on the IGate page. English: "Allowed prefixes (comma-separated)". */
#define TR_F_PREFIXES "Prefijos permitidos (separados por coma)"
/** Spanish text for the explanatory note shown beside the range prefix setting, rendered on the IGate page. English: "Local gate applied only to RF ->
 * Internet, independent of the payload-type filter above...". */
#define TR_NOTE_RANGE_PREFIX                                                                                                                                   \
    "Filtro local aplicado solo a RF -> Internet, independiente del filtro por tipo de carga anterior. La distancia se mide desde la posición de Mi "          \
    "Estación; los paquetes cuya posición no se puede decodificar no se ven afectados por el filtro de distancia."

/** Spanish text for the form label for the "3rdparty unwrap en" field or fieldset, rendered on the IGate page. English: "Relay whitelisted third-party (})
 * traffic". */
#define TR_F_3RDPARTY_UNWRAP_EN "Retransmitir tráfico de terceros (}) en lista blanca"
/** Spanish text for the explanatory note shown beside the 3rdparty unwrap setting, rendered on the IGate page. English: "Off by default. Only takes effect when
 * the Internet to RF Callsign Filter above is set...". */
#define TR_NOTE_3RDPARTY_UNWRAP                                                                                                                                \
    "Desactivado por defecto. Solo tiene efecto cuando el Filtro de Indicativos de Internet a RF anterior está en modo Lista Blanca: un paquete envuelto de "  \
    "terceros solo se desenvuelve y retransmite si su indicativo de origen interno está en la lista blanca. Active esto solo si confía en la fuente "          \
    "específica y la ha incluido en la lista blanca - volver a filtrar tráfico de terceros sin esta restricción es la causa más común de bucles de IGate."

/** Spanish text for the form label for the "log after filters" field or fieldset, rendered on the IGate page. English: "Log after filters". */
#define TR_F_LOG_AFTER_FILTERS "Registrar después de los filtros"
/** Spanish text for the explanatory note shown beside the log after filters setting, rendered on the IGate page. */
#define TR_NOTE_LOG_AFTER_FILTERS                                                                                                                              \
    "Desactivado, la tabla de tráfico y la consola serie informan todas las tramas decodificadas en RF y todas las líneas que envía el servidor APRS-IS. "     \
    "Activado, informan solo lo que aceptan los filtros IGate de esta estación: la Lista de Satélites Digipetidores y el juego RF a Internet para el "         \
    "tráfico de radio, el juego Internet a RF para el tráfico APRS-IS, y el Filtro de Indicativos para ambos. Solo acota lo que se muestra: una trama "        \
    "omitida se repite, se pasarela y se cuenta igual que antes."

/** Spanish text for the form label for the "satgate" field or fieldset, rendered on the IGate page. English: "Satellite Gate List". */
#define TR_F_SATGATE "Lista de Satélites Digipetidores"
/** Spanish text for the form label for the "satgate call" field or fieldset, rendered on the IGate page. English: "Satellite Callsign". */
#define TR_F_SATGATE_CALL "Indicativo de Satélite"
/** Spanish text for the explanatory note shown beside the satgate setting, rendered on the IGate page. English: "Callsigns of satellite/ISS digipeaters (e.g.
 * ISS, PSAT). A frame routed through one of...". */
#define TR_NOTE_SATGATE                                                                                                                                        \
    "Indicativos de satélites/ISS digipetidores (p. ej. ISS, PSAT). Un paquete enrutado por uno de estos solo se retransmite a APRS-IS si la entrada de "      \
    "trayectoria del digipetidor está realmente marcada como usada. Hasta 8 entradas; deje una entrada vacía para desactivarla."

/** Spanish text for the form label for the "dup cache" field or fieldset, rendered on the IGate page. English: "Duplicate Suppression". */
#define TR_F_DUP_CACHE "Supresión de Duplicados"
/** Spanish text for the form label for the "dup cache size" field or fieldset, rendered on the IGate page. English: "Cache Size (entries)". */
#define TR_F_DUP_CACHE_SIZE "Tamaño de Caché (entradas)"
/** Spanish text for the form label for the "dup cache timeout ms" field or fieldset, rendered on the IGate page. English: "Suppression Window (ms)". */
#define TR_F_DUP_CACHE_TIMEOUT_MS "Ventana de Supresión (ms)"
/** Spanish text for the explanatory note shown beside the dup cache setting, rendered on the IGate page. English: "Shared by the IGate and the Digipeater to
 * suppress repeated copies of the same frame. A...". */
#define TR_NOTE_DUP_CACHE                                                                                                                                      \
    "Compartido por el IGate y el Digipetidor para suprimir copias repetidas del mismo paquete. Un digipetidor ocupado en una frecuencia congestionada "       \
    "puede necesitar una caché más grande; un IGate rural con poco tráfico puede preferir una ventana más corta."

/** Spanish text for the form label for the "msg gating" field or fieldset, rendered on the IGate page. English: "Message Gating (Internet to RF)". */
#define TR_F_MSG_GATING "Filtrado de Mensajes (Internet a RF)"
/** Spanish text for the form label for the "msg gate en" field or fieldset, rendered on the IGate page. English: "Apply message gating criteria". */
#define TR_F_MSG_GATE_EN "Aplicar criterios de filtrado de mensajes"
/** Spanish text for the form label for the "msg local window s" field or fieldset, rendered on the IGate page. English: "Heard-locally window (s)". */
#define TR_F_MSG_LOCAL_WINDOW_S "Ventana de escucha local (s)"
/** Spanish text for the form label for the "msg max hops" field or fieldset, rendered on the IGate page. English: "Addressee hop limit (0 = direct only)". */
#define TR_F_MSG_MAX_HOPS "Límite de saltos del destinatario (0 = solo directo)"
/** Spanish text for the explanatory note shown beside the msg gating setting, rendered on the IGate page. English: "A message read from APRS-IS is put on the
 * air only when its addressee was heard on RF i...". */
#define TR_NOTE_MSG_GATING                                                                                                                                     \
    "Un mensaje leído de APRS-IS sale al aire solo si su destinatario fue escuchado por RF dentro de la ventana y con no más saltos que el límite, su "        \
    "remitente no fue escuchado por RF, la cabecera del remitente no lleva TCPXX/NOGATE/RFONLY y el destinatario no está a su vez en Internet. El "            \
    "siguiente reporte de posición de ese destinatario también se retransmite una vez, para poder ubicarlo. El límite de saltos es lo que evita transmitir "   \
    "a una estación que se oye a través de digipetidores lejanos pero a la que no se llega de vuelta; conviene ponerlo en la cantidad de saltos que "          \
    "recorre la propia ruta de transmisión. Si se desactiva, se transmite todo mensaje que permita el filtro de tipos, a destinatarios de cualquier parte "    \
    "del mundo."

/** Spanish text for the form label for the "digi aliases" field or fieldset, rendered on the IGate page. English: "n-N Path Aliases". */
#define TR_F_DIGI_ALIASES "Alias de Ruta n-N"
/** Spanish text for the form label for the "digi alias" field or fieldset, rendered on the IGate page. English: "Alias". */
#define TR_F_DIGI_ALIAS "Alias"
/** Spanish text for the form label for the "digi max n" field or fieldset, rendered on the IGate page. English: "Max N". */
#define TR_F_DIGI_MAX_N "N máximo"
/** Spanish text for the form label for the "digi alias mode" field or fieldset, rendered on the IGate page. English: "Mode". */
#define TR_F_DIGI_ALIAS_MODE "Modo"
/** Spanish text for the form label for the "digi fillin only" field or fieldset, rendered on the IGate page. English: "Fill-in digipeater (single hop only)".
 */
#define TR_F_DIGI_FILLIN_ONLY "Digipetidor de relleno (un solo salto)"
/** Spanish text for the form label for the "digi trap action" field or fieldset, rendered on the IGate page. English: "Hop count above Max N". */
#define TR_F_DIGI_TRAP_ACTION "Saltos por encima del N máximo"
/** Spanish text for the digipeater page label for trap clamp, rendered on the IGate page. English: "Clamp to Max N". */
#define TR_DIGI_TRAP_CLAMP "Limitar al N máximo"
/** Spanish text for the digipeater page label for trap drop, rendered on the IGate page. English: "Drop the frame". */
#define TR_DIGI_TRAP_DROP "Descartar la trama"
/** Spanish text for the digipeater page label for mode off, rendered on the IGate page. English: "Off". */
#define TR_DIGI_MODE_OFF "Apagado"
/** Spanish text for the digipeater page label for mode trace, rendered on the IGate page. English: "Trace (insert callsign)". */
#define TR_DIGI_MODE_TRACE "Traza (inserta indicativo)"
/** Spanish text for the digipeater page label for mode flood, rendered on the IGate page. English: "Flood (no callsign)". */
#define TR_DIGI_MODE_FLOOD "Inundación (sin indicativo)"
/** Spanish text for the form label for the "digi preempt" field or fieldset, rendered on the IGate page. English: "Explicit routes naming this station". */
#define TR_F_DIGI_PREEMPT "Rutas explícitas que nombran a esta estación"
/** Spanish text for the digipeater page label for preempt off, rendered on the IGate page. English: "Off (first unused address only)". */
#define TR_DIGI_PREEMPT_OFF "Apagado (solo la primera dirección sin usar)"
/** Spanish text for the digipeater page label for preempt mark, rendered on the IGate page. English: "Serve now, keep the skipped addresses". */
#define TR_DIGI_PREEMPT_MARK "Atender ya, conservando las direcciones salteadas"
/** Spanish text for the digipeater page label for preempt drop, rendered on the IGate page. English: "Serve now, discard the skipped addresses". */
#define TR_DIGI_PREEMPT_DROP "Atender ya, descartando las direcciones salteadas"
/** Spanish text for the form label for the "digi dest ssid" field or fieldset, rendered on the IGate page. English: "Digipeat by destination SSID (legacy)". */
#define TR_F_DIGI_DEST_SSID "Digipetir por SSID de destino (heredado)"
/** Spanish text for the explanatory note shown beside the digi aliases setting, rendered on the IGate page. English: "The only aliases this digipeater honours.
 * Write each one without its SSID; '#' matches...". */
#define TR_NOTE_DIGI_ALIASES                                                                                                                                   \
    "Los únicos alias que este digipetidor atiende. Escriba cada uno sin su SSID; '#' equivale a un solo dígito, así 'WIDE#' cubre toda la familia WIDEn. "    \
    "Las filas se prueban en orden y gana la primera coincidencia. Traza inserta el indicativo de esta estación para que cada salto sea identificable "        \
    "después, que es lo que exige WIDEn-N; inundación no deja rastro y solo conviene en un alias regional que se decida usar así."

/** Spanish text for the explanatory note shown beside the digi preempt setting, rendered on the IGate page. English: "Off is the safe default. When it is on,
 * the path is scanned from its first unused address...". */
#define TR_NOTE_DIGI_PREEMPT                                                                                                                                   \
    "Apagado es el valor seguro por omisión. Encendido, la ruta se recorre desde su primera dirección sin usar hasta el final buscando el indicativo de "      \
    "esta estación o alguno de los alias de arriba que no sea un nombre de la familia n-N, y una coincidencia más adelante se atiende de inmediato en vez "    \
    "de esperar a las direcciones que están delante. Eso es lo que hace funcionar una ruta explícita como WIDE1-1,CITYA,WIDE2-1,CITYB, que carga el canal "    \
    "mucho menos que una inundación WIDEn-N. Conservar las direcciones salteadas deja visible la ruta pedida; descartarlas pone al aire el camino restante "   \
    "más corto. Los alias n-N genéricos nunca se reclaman así en ninguno de los dos modos."

/** Spanish text for the explanatory note shown beside the digi dest ssid setting, rendered on the IGate page. English: "Off by default. When on, a frame whose
 * AX.25 destination SSID is 1 to 7 is repeated on...". */
#define TR_NOTE_DIGI_DEST_SSID                                                                                                                                 \
    "Desactivado por omisión. Cuando está activo, una trama cuyo SSID de destino AX.25 sea de 1 a 7 se repite por ese solo SSID, antes de consultar la tabla " \
    "de alias de arriba, así que se ignora la ruta que pidió la estación de origen. Actívelo solo por un vecino heredado que todavía rutee de esta manera."

/** Spanish text for the symbol-picker label for icon, rendered on the IGate page. English: "Icon". */
#define TR_SYM_ICON "Icono"
/** Spanish text for the symbol-picker label for quick pick, rendered on the IGate page. English: "Quick Pick". */
#define TR_SYM_QUICK_PICK "Selección Rápida"
/** Spanish text for the symbol-picker label for primary table, rendered on the IGate page. English: "Primary Table ( / )". */
#define TR_SYM_PRIMARY_TABLE "Tabla Primaria ( / )"
/** Spanish text for the symbol-picker label for alternate table, rendered on the IGate page. English: "Alternate Table ( \ )". */
#define TR_SYM_ALTERNATE_TABLE "Tabla Alternativa ( \\ )"
/** Spanish text for the symbol-picker label for tracker, rendered on the IGate page. English: "Tracker:". */
#define TR_SYM_TRACKER "Tracker:"

/** @} */

/**
 * @name GPS page
 * @{
 */
/** Spanish text for the page title of the GPS page, rendered on the GPS page. English: "GPS Receiver". */
#define TR_F_GPS "Receptor GPS"
/** Spanish text for the fieldset legend for the receiver enable block, rendered on the GPS page. English: "GPS Receiver". */
#define TR_GPS_FS_SERVICE "Receptor GPS"
/** Spanish text for the label of the receiver enable checkbox, rendered on the GPS page. English: "Enable GPS Receiver". */
#define TR_GPS_ENABLE "Habilitar Receptor GPS"
/** Spanish text for the fieldset legend for the receiver status block, rendered on the GPS page. English: "Receiver Status". */
#define TR_GPS_FS_STATUS "Estado del Receptor"
/** Spanish text for the fieldset legend for the position block, rendered on the GPS page. English: "Position". */
#define TR_GPS_FS_POSITION "Posición"
/** Spanish text for the fieldset legend for the motion block, rendered on the GPS page. English: "Motion". */
#define TR_GPS_FS_MOTION "Movimiento"
/** Spanish text for the fieldset legend for the date/time block, rendered on the GPS page. English: "Date and Time (UTC)". */
#define TR_GPS_FS_TIME "Fecha y Hora (UTC)"
/** Spanish text for the fieldset legend for the satellites/accuracy block, rendered on the GPS page. English: "Satellites and Accuracy". */
#define TR_GPS_FS_SATELLITES "Satélites y Precisión"
/** Spanish text for the fieldset legend for the serial link statistics block, rendered on the GPS page. English: "Serial Link". */
#define TR_GPS_FS_LINK "Enlace Serie"
/** Spanish text for the fieldset legend for the compile-time wiring block, rendered on the GPS page. English: "Wiring (compile-time)". */
#define TR_GPS_FS_WIRING "Cableado (tiempo de compilación)"
/** Spanish text for the row label for the coloured module-status badge summarising the serial link, rendered on the GPS page. English: "Module Status". */
#define TR_GPS_MODULE_STATUS "Estado del Módulo"
/** Spanish text for the row label for the receiver link state, rendered on the GPS page. English: "Link". */
#define TR_GPS_LINK "Enlace"
/** Spanish text for the row label for the navigation status reported by RMC, rendered on the GPS page. English: "Navigation Status". */
#define TR_GPS_NAV_STATUS "Estado de Navegación"
/** Spanish text for the row label for the fix quality reported by GGA, rendered on the GPS page. English: "Fix Quality". */
#define TR_GPS_FIX_QUALITY "Calidad del Fix"
/** Spanish text for the row label for the fix mode reported by GSA, rendered on the GPS page. English: "Fix Mode". */
#define TR_GPS_FIX_MODE "Modo del Fix"
/** Spanish text for the row label for latitude, rendered on the GPS page. English: "Latitude". */
#define TR_GPS_LATITUDE "Latitud"
/** Spanish text for the row label for longitude, rendered on the GPS page. English: "Longitude". */
#define TR_GPS_LONGITUDE "Longitud"
/** Spanish text for the row label for altitude above mean sea level, rendered on the GPS page. English: "Altitude (MSL)". */
#define TR_GPS_ALTITUDE "Altitud (MSL)"
/** Spanish text for the row label for geoid separation, rendered on the GPS page. English: "Geoid Separation". */
#define TR_GPS_GEOID "Separación del Geoide"
/** Spanish text for the row label for ground speed, rendered on the GPS page. English: "Ground Speed". */
#define TR_GPS_SPEED "Velocidad sobre el Suelo"
/** Spanish text for the row label for course over ground, rendered on the GPS page. English: "Course (true)". */
#define TR_GPS_COURSE "Rumbo (verdadero)"
/** Spanish text for the row label for magnetic variation, rendered on the GPS page. English: "Magnetic Variation". */
#define TR_GPS_MAGVAR "Variacion Magnetica"
/** Spanish text for the row label for the UTC date of the fix, rendered on the GPS page. English: "Date". */
#define TR_GPS_UTC_DATE "Fecha"
/** Spanish text for the row label for the UTC time of the fix, rendered on the GPS page. English: "Time". */
#define TR_GPS_UTC_TIME "Hora"
/** Spanish text for the row label for the satellite count used in the solution, rendered on the GPS page. English: "Satellites Used". */
#define TR_GPS_SATS_USED "Satélites Usados"
/** Spanish text for the row label for the satellite count in view, rendered on the GPS page. English: "Satellites in View". */
#define TR_GPS_SATS_IN_VIEW "Satélites a la Vista"
/** Spanish text for the row label for horizontal dilution of precision, rendered on the GPS page. English: "HDOP (horizontal)". */
#define TR_GPS_HDOP "HDOP (horizontal)"
/** Spanish text for the row label for position dilution of precision, rendered on the GPS page. English: "PDOP (position)". */
#define TR_GPS_PDOP "PDOP (posicion)"
/** Spanish text for the row label for vertical dilution of precision, rendered on the GPS page. English: "VDOP (vertical)". */
#define TR_GPS_VDOP "VDOP (vertical)"
/** Spanish text for the row label for the count of sentences whose checksum verified, rendered on the GPS page. English: "Sentences Accepted". */
#define TR_GPS_SENTENCES_OK "Sentencias Aceptadas"
/** Spanish text for the row label for the count of sentences discarded on a checksum error, rendered on the GPS page. English: "Sentences Discarded". */
#define TR_GPS_SENTENCES_BAD "Sentencias Descartadas"
/** Spanish text for the row label for the time since the last valid sentence, rendered on the GPS page. English: "Since Last Sentence". */
#define TR_GPS_LINK_AGE "Desde la Última Sentencia"
/** Spanish text for the row label for the time since the last position fix, rendered on the GPS page. English: "Since Last Fix". */
#define TR_GPS_FIX_AGE "Desde el Último Fix"
/** Spanish text for the row label for the UART port number the receiver is wired to, rendered on the GPS page. English: "Serial Port". */
#define TR_GPS_PORT "Puerto Serie"
/** Spanish text for the row label for the ESP32 receive pin, rendered on the GPS page. English: "Receive Pin (module TX)". */
#define TR_GPS_RX_PIN "Pin de Recepción (TX del módulo)"
/** Spanish text for the row label for the ESP32 transmit pin, rendered on the GPS page. English: "Transmit Pin (module RX)". */
#define TR_GPS_TX_PIN "Pin de Transmisión (RX del módulo)"
/** Spanish text for the row label for the serial line rate, rendered on the GPS page. English: "Line Rate". */
#define TR_GPS_BAUD "Velocidad de Línea"
/** Spanish text for the link state shown while sentences are arriving, rendered on the GPS page. English: "Receiving". */
#define TR_GPS_LINK_RECEIVING "Recibiendo"
/** Spanish text for the link state shown when the receiver has gone silent, rendered on the GPS page. English: "No data from receiver". */
#define TR_GPS_LINK_SILENT "Sin datos del receptor"
/** Spanish text for the module-status badge text shown when the receiver is switched off or its UART failed to come up, rendered on the GPS page. English:
 * "Disabled". */
#define TR_GPS_STATUS_DISABLED "Deshabilitado"
/** Spanish text for the module-status badge text shown when the receiver is enabled but no sentence has arrived within the link timeout, rendered on the GPS
 * page. English: "No data (check wiring)". */
#define TR_GPS_STATUS_NO_LINK "Sin datos (revisar cableado)"
/** Spanish text for the module-status badge text shown when sentences are arriving but no valid fix has been reported yet, rendered on the GPS page. English:
 * "Searching (no fix)". */
#define TR_GPS_STATUS_SEARCHING "Buscando (sin fijación)"
/** Spanish text for the module-status badge text shown when sentences are arriving and the last navigation solution is valid, rendered on the GPS page.
 * English: "Fix OK". */
#define TR_GPS_STATUS_FIX_OK "Fijación OK"
/** Spanish text for the navigation status shown when RMC reports an active solution, rendered on the GPS page. English: "Active". */
#define TR_GPS_NAV_ACTIVE "Activa"
/** Spanish text for the navigation status shown when RMC reports a warning, rendered on the GPS page. English: "Warning (no valid fix)". */
#define TR_GPS_NAV_WARNING "Advertencia (sin fix válido)"
/** Spanish text for the fix quality shown when the receiver has no fix, rendered on the GPS page. English: "No fix". */
#define TR_GPS_Q_NONE "Sin fix"
/** Spanish text for the fix quality shown for an autonomous fix, rendered on the GPS page. English: "GPS (autonomous)". */
#define TR_GPS_Q_GPS "GPS (autónomo)"
/** Spanish text for the fix quality shown for a differentially corrected fix, rendered on the GPS page. English: "DGPS (differential)". */
#define TR_GPS_Q_DGPS "DGPS (diferencial)"
/** Spanish text for the fix quality shown for a precise positioning service fix, rendered on the GPS page. English: "PPS". */
#define TR_GPS_Q_PPS "PPS"
/** Spanish text for the fix quality shown for a fixed-ambiguity RTK solution, rendered on the GPS page. English: "RTK (fixed)". */
#define TR_GPS_Q_RTK "RTK (fijo)"
/** Spanish text for the fix quality shown for a float RTK solution, rendered on the GPS page. English: "RTK (float)". */
#define TR_GPS_Q_RTK_FLOAT "RTK (flotante)"
/** Spanish text for the fix quality shown for a dead-reckoning estimate, rendered on the GPS page. English: "Estimated (dead reckoning)". */
#define TR_GPS_Q_ESTIMATED "Estimado (navegación a estima)"
/** Spanish text for the fix quality shown for manual input mode, rendered on the GPS page. English: "Manual input". */
#define TR_GPS_Q_MANUAL "Entrada manual"
/** Spanish text for the fix quality shown for simulation mode, rendered on the GPS page. English: "Simulated". */
#define TR_GPS_Q_SIMULATED "Simulado"
/** Spanish text for the fix mode shown when no solution has been computed, rendered on the GPS page. English: "No fix". */
#define TR_GPS_M_NOFIX "Sin fix"
/** Spanish text for the fix mode shown for a horizontal-only solution, rendered on the GPS page. English: "2D (horizontal only)". */
#define TR_GPS_M_2D "2D (solo horizontal)"
/** Spanish text for the fix mode shown for a solution including altitude, rendered on the GPS page. English: "3D (with altitude)". */
#define TR_GPS_M_3D "3D (con altitud)"

/** Sidebar menu entry for the BrandMeister page. */
#define TR_MENU_BM "BrandMeister"
/** Page title of the BrandMeister interconnect page. */
#define TR_F_BM "BrandMeister"
/** Fieldset legend for the interconnect enable switch, rendered on the BrandMeister page. */
#define TR_BM_FS_SERVICE "Interconexion BrandMeister"
/** Form label for the interconnect enable switch, rendered on the BrandMeister page. */
#define TR_BM_ENABLE "Habilitar la interconexion BrandMeister"
/** Explanatory note shown beside the interconnect enable switch, rendered on the BrandMeister page. */
#define TR_BM_NOTE_SERVICE                                                                                                                                     \
    "El lado APRS de BrandMeister es un cliente APRS-IS: cada master inyecta en APRS-IS el trafico originado en DMR como paquetes comunes. Esta "              \
    "estacion no necesita ninguna conexion DMR ni cuenta de BrandMeister: la interconexion viaja por la sesion APRS-IS que el IGate ya tiene. Con "            \
    "esta opcion apagada no se clasifica ninguna linea y el ruteo de mensajes queda intacto."
/** Fieldset legend for the worldwide monitor subscription, rendered on the BrandMeister page. */
#define TR_BM_FS_MONITOR "Monitor mundial"
/** Form label for the worldwide monitor switch, rendered on the BrandMeister page. */
#define TR_BM_MONITOR "Suscribirse al trafico BrandMeister mundial"
/** Explanatory note shown beside the worldwide monitor switch, rendered on the BrandMeister page. */
#define TR_BM_NOTE_MONITOR                                                                                                                                     \
    "Los terminos del filtro de servidor APRS-IS se combinan con O, nunca con Y, asi que el servidor no puede acotar esta suscripcion a su zona. Se "          \
    "rechaza mientras el reenvio Internet a RF este activo y el filtro de rango Internet a RF de la pagina IGate este apagado, porque no quedaria "            \
    "nada entre un flujo mundial y su transmisor."
/** Lead-in to the server filter term the monitor subscription needs, rendered on the BrandMeister page. */
#define TR_BM_NOTE_FILTER_TERM "Agregue este termino al filtro de servidor de la pagina IGate para recibir la suscripcion:"
/** Warning shown once when the monitor switch was refused for want of the range gate, rendered on the BrandMeister page. */
#define TR_BM_WARN_NEEDS_RANGE "Rechazado: habilite primero el filtro de rango Internet a RF en la pagina IGate."
/** Fieldset legend for message routing, rendered on the BrandMeister page. */
#define TR_BM_FS_MESSAGING "Mensajeria"
/** Form label for the Internet-only message routing switch, rendered on the BrandMeister page. */
#define TR_BM_MSG_INET_ONLY "Enviar mensajes a estaciones BrandMeister solo por Internet"
/** Explanatory note shown beside the Internet-only message routing switch, rendered on the BrandMeister page. */
#define TR_BM_NOTE_MSG_INET_ONLY                                                                                                                               \
    "Una estacion escuchada por ultima vez como trafico BrandMeister esta en la red, no en el canal local, asi que una copia por RF de un mensaje "            \
    "dirigido a ella es tiempo de aire gastado en un receptor que no esta ahi. Esto nunca agrega la pata de Internet: con \"Enviar a Internet\" "              \
    "apagado en la pagina Mensajes, no se envia nada."
/** Note on delivery not being guaranteed, rendered on the BrandMeister page. */
#define TR_BM_NOTE_DELIVERY                                                                                                                                    \
    "La entrega no esta garantizada y la falla es silenciosa: cada master BrandMeister aplica su propio patron al destinatario de los mensajes "               \
    "entrantes, y esta estacion no puede verlo. Un mensaje filtrado simplemente no produce acuse. Ademas el destinatario debe ser el indicativo "              \
    "asociado al ID DMR en el SelfCare del receptor."
/** Fieldset legend for the gateway callsign list, rendered on the BrandMeister page. */
#define TR_BM_FS_GATEWAYS "Indicativos de pasarela"
/** Form label prefix for one gateway callsign slot, rendered on the BrandMeister page. */
#define TR_BM_GATEWAY "Pasarela"
/** Explanatory note shown beside the gateway callsign list, rendered on the BrandMeister page. */
#define TR_BM_NOTE_GATEWAYS                                                                                                                                    \
    "Opcional. Se compara con la estacion de entrada que sigue al q construct, para quien quiera marcar solo el trafico de su propio master. Un * "            \
    "final compara por prefijo. Dejelo vacio para reconocer el trafico BrandMeister solo por su destino APBMxx y por el elemento DMR de la ruta, "             \
    "que no necesita configuracion."
/** Fieldset legend for the read-only status table, rendered on the BrandMeister page. */
#define TR_BM_FS_STATUS "Estado"
/** Status row label for the interconnect switch, rendered on the BrandMeister page. */
#define TR_BM_ST_SERVICE "Interconexion"
/** Status row label for the monitor term in the server filter, rendered on the BrandMeister page. */
#define TR_BM_ST_FILTER_TERM "Termino de monitor en el filtro de servidor"
/** Status row label for the Internet to RF range gate, rendered on the BrandMeister page. */
#define TR_BM_ST_RANGE_GATE "Filtro de rango Internet a RF"
/** Status row label for the count of BrandMeister stations heard, rendered on the BrandMeister page. */
#define TR_BM_ST_STATIONS "Estaciones BrandMeister escuchadas"
/** Status row value for an enabled service, rendered on the BrandMeister page. */
#define TR_BM_ST_ON "Habilitada"
/** Status row value for a disabled service, rendered on the BrandMeister page. */
#define TR_BM_ST_OFF "Deshabilitada"
/** Status row value for a filter term that is present, rendered on the BrandMeister page. */
#define TR_BM_ST_PRESENT "Presente"
/** Status row value for a filter term that is missing, rendered on the BrandMeister page. */
#define TR_BM_ST_ABSENT "Ausente"
/** Status row value for a range gate that governs nothing, rendered on the BrandMeister page. */
#define TR_BM_ST_GATE_NA "No aplica (el reenvio Internet a RF esta apagado)"
/** Status row value for a range gate that is off, rendered on the BrandMeister page. */
#define TR_BM_ST_GATE_OFF "Apagado"
/** Explanatory note shown under the status table, rendered on the BrandMeister page. */
#define TR_BM_NOTE_STATUS                                                                                                                                      \
    "Las ultimas tres filas se gobiernan desde la pagina IGate. Las estaciones BrandMeister se cuentan en la tabla LAST HEARD, donde llevan el prefijo BM."
/** Explanatory note shown beside the Internet to RF range gate, rendered on the IGate page. */
#define TR_NOTE_INET2RF_RANGE                                                                                                                                  \
    "Filtro local aplicado solo a Internet -> RF, independiente del filtro de tipo de payload de arriba. El rango se mide desde la posicion de Mi "            \
    "Estacion; las lineas cuya posicion no se puede decodificar no se ven afectadas. Es requisito para reenviar al transmisor cualquier "                      \
    "suscripcion mundial."

/** @} */

/**
 * @name Telegram page
 * @{
 */
/** Page title of the Telegram page, rendered on the Telegram page. */
#define TR_F_TELEGRAM "Bot de Telegram"
/** Fieldset legend for the bot enable block, rendered on the Telegram page. */
#define TR_TG_FS_SERVICE "Bot de Telegram"
/** Label of the bot enable checkbox, rendered on the Telegram page. */
#define TR_TG_ENABLE "Habilitar bot de Telegram"
/** Explanatory note shown under the enable switch, rendered on the Telegram page. */
#define TR_TG_NOTE_SERVICE                                                                                                                                     \
    "Con esto apagado no se conecta nada a Telegram ni corre ninguna tarea de sondeo. Encenderlo o apagarlo tiene efecto inmediato, sin reiniciar. El bot "    \
    "necesita conexion a Internet y memoria libre suficiente para una sesion TLS."
/** Label of the route-station-messages checkbox, rendered on the Telegram page. */
#define TR_TG_ROUTE_MESSAGES "Reenviar mensajes de la estacion"
/** Explanatory note shown under the route-station-messages switch, rendered on the Telegram page. */
#define TR_TG_NOTE_ROUTE_MESSAGES                                                                                                                              \
    "Con esto encendido, un mensaje APRS recibido dirigido al Indicativo propio de uno de los usuarios autorizados de abajo se envia al chat de "              \
    "Telegram de ese usuario como \"msg from <remitente> to <destinatario> :: <texto del mensaje>\". El destinatario se compara solo con los campos "          \
    "Indicativo de esos usuarios, nunca con el My Callsign de esta estacion de la pagina Station, asi cada usuario recibe los mensajes dirigidos a su "        \
    "propio indicativo y a ningun otro. La coincidencia es exacta, SSID incluido, asi varios usuarios pueden compartir un mismo indicativo base con "          \
    "distinto SSID. Un mensaje cuyo destinatario no coincide con el Indicativo de ningun usuario no se reenvia a nadie. Las confirmaciones y los "             \
    "mensajes dirigidos a un grupo nunca se reenvian."
/** Label of the route-bulletins checkbox, rendered on the Telegram page. */
#define TR_TG_ROUTE_BULLETINS "Reenviar boletines"
/** Explanatory note shown under the route-bulletins switch, rendered on the Telegram page. */
#define TR_TG_NOTE_ROUTE_BULLETINS                                                                                                                             \
    "Con esto encendido, todo boletin APRS recibido en la red, por el aire o desde el flujo APRS-IS, se envia a todos los usuarios autorizados, al "           \
    "administrador y a todos los chats de grupo permitidos de mas abajo, como \"bulletin from <remitente> to <boletin> :: <texto del boletin>\". Un "          \
    "boletin es un destinatario \"BLN\" seguido de un digito o una letra y, opcionalmente, de un nombre de grupo; no se compara ningun indicativo, ya que "    \
    "un boletin esta dirigido a toda la red y no a una estacion. Un boletin identico a otro ya reenviado dentro de la ventana de repeticion de mas abajo "     \
    "no se envia de nuevo, asi un boletin que su emisor repite por temporizador, o que se escucha a traves de varios digipetidores, llega una sola vez "       \
    "a cada chat."
/** Label of the bulletin repeat window field, rendered on the Telegram page. */
#define TR_TG_BULLETIN_WINDOW "Ventana de repeticion de boletines (s)"
/** Explanatory note shown under the bulletin repeat window field, rendered on the Telegram page. */
#define TR_TG_NOTE_BULLETIN_WINDOW                                                                                                                             \
    "Cuanto tiempo un boletin ya reenviado impide que se reenvien tambien sus repeticiones. Un boletin cuyo remitente, destinatario y texto coinciden "        \
    "con otro ya entregado dentro de estos segundos se descarta; cambiar el texto, o que lo envie otra estacion, lo convierte en un boletin nuevo y se "       \
    "reenvia de inmediato. Pongala mas larga que el intervalo con que se transmiten los boletines de este canal, asi cada uno llega a los chats una vez "      \
    "por edicion y no una vez por transmision. 0 apaga la comprobacion y reenvia todas las copias, incluidas las que vuelven por los digipetidores y "         \
    "desde el flujo APRS-IS. Se recuerdan los ocho boletines reenviados mas recientes, sea cual sea esta ventana."
/** Fieldset legend for the credentials block, rendered on the Telegram page. */
#define TR_TG_FS_BOT "Credenciales"
/** Label of the bot token field, rendered on the Telegram page. */
#define TR_TG_TOKEN "Token del bot"
/** Label of the administrator identifier field, rendered on the Telegram page. */
#define TR_TG_ADMIN_ID "ID del administrador"
/** Explanatory note shown under the credentials, rendered on the Telegram page. */
#define TR_TG_NOTE_ADMIN                                                                                                                                       \
    "El token lo emite @BotFather. El identificador del administrador es un numero, no un nombre de usuario: envie cualquier comando al bot desde su propia "  \
    "cuenta y el bot responde con ese numero, que ademas queda escrito en el log. Dejelo en 0 para no agregar ningun administrador aqui."
/** Warning shown under the credentials when the bot is enabled with no administrator identifier, rendered on the Telegram page. */
#define TR_TG_WARN_NO_ADMIN                                                                                                                                    \
    "El bot esta habilitado pero no hay identificador de administrador configurado. Nadie tiene permisos de administrador y, salvo que haya un usuario "       \
    "autorizado en la lista de abajo, el bot rechaza a todos los remitentes. Envie cualquier comando al bot desde su propia cuenta: el rechazo que responde "  \
    "incluye su identificador numerico, que es lo que va en el campo de arriba."
/** Fieldset legend for the Mini App block, rendered on the Telegram page. */
#define TR_TG_FS_MINIAPP "Mini App"
/** Label of the Mini App address field, rendered on the Telegram page. */
#define TR_TG_MINIAPP_URL "Direccion de la Mini App"
/** Explanatory note shown under the Mini App address, rendered on the Telegram page. */
#define TR_TG_NOTE_MINIAPP                                                                                                                                     \
    "La direccion HTTPS de una Mini App de Telegram que abre el boton de menu del bot. Dejela vacia para ejecutar el bot sin boton de Mini App."
/** Fieldset legend for the authorized-users block, rendered on the Telegram page. */
#define TR_TG_FS_USERS "Usuarios autorizados"
/** Explanatory note shown above the authorized-users table, rendered on the Telegram page. */
#define TR_TG_NOTE_USERS                                                                                                                                       \
    "Hasta 8 usuarios, ademas del administrador, que pueden hablar con el bot como ellos mismos en lugar de ser rechazados por no estar autorizados. "         \
    "El bot responde a un comando de una cuenta que no conoce con el identificador de esa cuenta, que es el numero a ingresar aqui; una cuenta ya "            \
    "autorizada lo obtiene con /whoami. Un identificador vacio deja la ranura sin usar. Indicativo es el indicativo de radioaficionado propio de ese "         \
    "operador; es el destinatario por el que \"Reenviar mensajes de la estacion\" elige a este usuario y no se usa para nada mas."
/** Fieldset legend for the allowed-group-chats block, rendered on the Telegram page. */
#define TR_TG_FS_CHATS "Chats de grupo permitidos"
/** Explanatory note shown above the allowed-group-chats table, rendered on the Telegram page. */
#define TR_TG_NOTE_CHATS                                                                                                                                       \
    "Hasta 4 chats de grupo en los que el bot puede responder. El identificador de un supergrupo es un numero negativo grande; envie /whoami al bot desde "    \
    "dentro del grupo para obtenerlo. Un identificador vacio deja la ranura sin usar."
/** Legend format ("User %d") for one authorized-user accordion card, rendered on the Telegram page. */
#define TR_TG_F_USER_FMT "Usuario %d"
/** Legend format ("Chat %d") for one allowed-group-chat accordion card, rendered on the Telegram page. */
#define TR_TG_F_CHAT_FMT "Chat %d"
/** Label of one entry's identifier field, rendered on the Telegram page. */
#define TR_TG_F_PEER_ID "Identificador"
/** Label of one entry's display-name field, rendered on the Telegram page. */
#define TR_TG_F_PEER_NAME "Nombre"
/** Label of one authorized user's own callsign field, rendered on the Telegram page. */
#define TR_TG_F_USER_CALLSIGN "Indicativo"
/** Fieldset legend for the connection status block, rendered on the Telegram page. */
#define TR_TG_FS_STATUS "Estado de la conexion"
/** Status row label for the coarse state, rendered on the Telegram page. */
#define TR_TG_ST_STATE "Estado"
/** Status row label for the reason behind the state, rendered on the Telegram page. */
#define TR_TG_ST_REASON "Diagnostico"
/** Status row label for the untranslated detail, rendered on the Telegram page. */
#define TR_TG_ST_DETAIL "Detalle"
/** Status row label for the bot user name, rendered on the Telegram page. */
#define TR_TG_ST_BOT "Bot"
/** Status row label for the time the service has been polling, rendered on the Telegram page. */
#define TR_TG_ST_UPTIME "Tiempo en marcha"
/** Status row label for the count of decoded updates, rendered on the Telegram page. */
#define TR_TG_ST_UPDATES "Actualizaciones recibidas"
/** Status row label for the count of dispatched commands, rendered on the Telegram page. */
#define TR_TG_ST_COMMANDS "Comandos atendidos"
/** Status row label for the count of accepted outgoing messages, rendered on the Telegram page. */
#define TR_TG_ST_SENT "Mensajes enviados"
/** Status row label for the count of updates from unauthorized senders, rendered on the Telegram page. */
#define TR_TG_ST_REJECTED "No autorizados rechazados"
/** Status row label for the consecutive polling failure count, rendered on the Telegram page. */
#define TR_TG_ST_POLL_ERRORS "Errores de sondeo seguidos"
/** Explanatory note shown under the status table, rendered on the Telegram page. */
#define TR_TG_NOTE_STATUS                                                                                                                                      \
    "Se refresca cada dos segundos. Los contadores se reinician cada vez que arranca el servicio. La fila de detalle lleva una ruta de archivo, un nombre de " \
    "error de ESP-IDF o el texto que devolvio Telegram, y se muestra sin traducir a proposito."
/** Note naming the file the whole configuration lives in, rendered on the Telegram page. */
#define TR_TG_NOTE_FILE "Todo lo anterior se guarda en este archivo, que tambien se puede descargar, editar y volver a subir desde la pagina Almacenamiento:"
/** Coarse state shown while the bot is switched off, rendered on the Telegram page. */
#define TR_TG_STATE_DISABLED "Deshabilitado"
/** Coarse state shown while bring-up is in progress, rendered on the Telegram page. */
#define TR_TG_STATE_STARTING "Iniciando"
/** Coarse state shown while the bot is polling Telegram, rendered on the Telegram page. */
#define TR_TG_STATE_RUNNING "En marcha"
/** Coarse state shown when bring-up stopped at a fault, rendered on the Telegram page. */
#define TR_TG_STATE_ERROR "Error"
/** Diagnosis shown when the switch is off, rendered on the Telegram page. */
#define TR_TG_R_DISABLED "El bot esta apagado en esta pagina."
/** Diagnosis shown when the settings file is absent, rendered on the Telegram page. */
#define TR_TG_R_FILE_MISSING                                                                                                                                   \
    "El archivo de configuracion no esta en la particion de almacenamiento. Guarde esta pagina una vez para crearlo, o subalo desde la pagina Almacenamiento."
/** Diagnosis shown when the settings file does not parse, rendered on the Telegram page. */
#define TR_TG_R_FILE_CORRUPT                                                                                                                                   \
    "El archivo de configuracion no es JSON valido. Se dejo intacto para que pueda examinarlo: descarguelo desde la pagina Almacenamiento, corrijalo y "       \
    "vuelva a subirlo, o guarde esta pagina para sobrescribirlo."
/** Diagnosis shown when the settings file could not be read into memory, rendered on the Telegram page. */
#define TR_TG_R_FILE_UNREADABLE                                                                                                                                \
    "El archivo de configuracion no se pudo leer en memoria. Lo mas probable es que el archivo este bien y que la memoria se haya agotado por un momento; a "  \
    "proposito no se sobrescribio. Reinicie la estacion y mire la memoria libre en el panel."
/** Diagnosis shown when no token is configured, rendered on the Telegram page. */
#define TR_TG_R_NO_TOKEN "No hay token configurado. Cree un bot con @BotFather y pegue arriba el token que le entregue."
/** Diagnosis shown when the token is not of the expected shape, rendered on the Telegram page. */
#define TR_TG_R_TOKEN_MALFORMED                                                                                                                                \
    "El token no tiene la forma <numeros>:<secreto>. Lo mas probable es que se haya pegado incompleto, partido en dos lineas, o copiado junto con las "        \
    "comillas. Vuelva a pegarlo desde @BotFather."
/** Diagnosis shown when the root certificate file is absent, rendered on the Telegram page. */
#define TR_TG_R_CERT_MISSING                                                                                                                                   \
    "El certificado raiz que valida api.telegram.org no esta en la particion de almacenamiento. Subalo, como archivo PEM, a la ruta que muestra la fila de "   \
    "detalle usando la pagina Almacenamiento. Sin el no se puede establecer ninguna conexion TLS."
/** Diagnosis shown when the root certificate file is unusable, rendered on the Telegram page. */
#define TR_TG_R_CERT_INVALID                                                                                                                                   \
    "El archivo del certificado raiz esta vacio, es mas grande de lo que acepta este firmware, o no contiene un certificado PEM. Suba un PEM plano, que "      \
    "empiece con la linea BEGIN CERTIFICATE, desde la pagina Almacenamiento."
/** Diagnosis shown while there is no route to the Internet, rendered on the Telegram page. */
#define TR_TG_R_WAITING_NETWORK                                                                                                                                \
    "Esperando una ruta a Internet. La estacion todavia no tiene direccion IP, asi que no se puede resolver ningun nombre ni abrir ninguna sesion TLS. Mire "  \
    "la pagina Inalambrico: el bot necesita conexion como estacion, un punto de acceso propio no alcanza."
/** Diagnosis shown when the host name could not be resolved, rendered on the Telegram page. */
#define TR_TG_R_DNS_FAILED                                                                                                                                     \
    "No se pudo resolver api.telegram.org. La estacion tiene direccion pero no un servidor de nombres que funcione: revise el DNS que entrega su router y "    \
    "que el puerto 53 de salida no este bloqueado. La fila de detalle indica cuanto corrio la consulta antes de rendirse."
/** Diagnosis shown when a plain TCP connection could not be opened, rendered on the Telegram page. */
#define TR_TG_R_TCP_FAILED                                                                                                                                     \
    "El nombre se resolvio pero no se pudo abrir ninguna conexion TCP al puerto 443. La ruta a Internet esta caida, o el HTTPS de salida esta bloqueado por "  \
    "un firewall o un portal cautivo. La fila de detalle lleva la direccion probada, el tiempo transcurrido y el numero de error del socket."
/** Diagnosis shown when the heap could not satisfy the bring-up, rendered on the Telegram page. */
#define TR_TG_R_NO_MEMORY                                                                                                                                      \
    "No hay memoria libre suficiente para una sesion TLS. Es el recurso mas ajustado de esta placa: una sesion cuesta decenas de kilobytes y el modem de "     \
    "radio, la pila WiFi y este servidor web ya tienen la suya. Vigile la memoria libre en el panel y considere apagar algun servicio que no use."
/** Diagnosis shown when the service refused to initialize, rendered on the Telegram page. */
#define TR_TG_R_INIT_FAILED                                                                                                                                    \
    "El servicio de Telegram no pudo inicializarse. La fila de detalle lleva el error exacto de ESP-IDF junto con la memoria libre y el bloque contiguo mas "  \
    "grande, asi que un par de numeros sano descarta la memoria; una linea de tabla llena en el log serie nombra la tabla de tamano fijo que hay que "         \
    "agrandar. Se reintentara dentro de un minuto."
/** Diagnosis shown when the call to Telegram did not complete, rendered on the Telegram page. */
#define TR_TG_R_CONNECT_FAILED                                                                                                                                 \
    "La estacion no pudo completar una llamada a api.telegram.org. La fila de detalle lleva el error exacto de ESP-IDF. Las causas tipicas son una conexion "  \
    "a Internet bloqueada o filtrada, una resolucion de nombres que falla, o un saludo TLS rechazado porque el certificado raiz no corresponde a la cadena "   \
    "del servidor. Se reintentara dentro de un minuto."
/** Diagnosis shown when Telegram answered and refused, rendered on the Telegram page. */
#define TR_TG_R_API_REJECTED                                                                                                                                   \
    "Telegram respondio y rechazo el token. La fila de detalle lleva el codigo de error y el texto que devolvio Telegram; 401 Unauthorized significa que el "  \
    "token es incorrecto o fue revocado, 404 Not Found que el bot ya no existe. Corrija el token de arriba; esto no se reintenta solo."
/** Diagnosis shown when the polling task could not be created, rendered on the Telegram page. */
#define TR_TG_R_TASK_FAILED "No se pudo crear la tarea de sondeo, lo que en esta placa siempre significa que la memoria no alcanzo para su pila."
/** Diagnosis shown while the bot is connected and polling, rendered on the Telegram page. */
#define TR_TG_R_CONNECTED "Conectado a Telegram y sondeando actualizaciones."

/** @} */

/**
 * @name Logs page
 * @{
 */
/** Spanish text for the page title of the Logs page, rendered on the Logs page. English: "Console Logs". */
#define TR_F_LOGS "Registros de consola"
/** Spanish text for the fieldset legend for the console log window, rendered on the Logs page. English: "Console Log". */
#define TR_LOGS_FS_CONSOLE "Registro de consola"
/** Spanish text for the caption of the button while nothing is being captured, rendered on the Logs page. English: "Start". */
#define TR_LOGS_BTN_START "Iniciar"
/** Spanish text for the caption of the button while the console log is being captured, rendered on the Logs page. English: "Stop". */
#define TR_LOGS_BTN_STOP "Detener"
/** Spanish text for the explanatory note shown above the console log window, rendered on the Logs page. English: "Start mirrors everything the station prints
 * on its serial console...". */
#define TR_LOGS_NOTE                                                                                                                                                \
    "Iniciar copia en la ventana de abajo todo lo que la estación imprime en su consola serie, de modo que puede leerse sin tener un cable serie conectado. "      \
    "La ventana guarda las últimas 50 líneas y se desplaza; una línea de más de 255 caracteres continúa en la siguiente. Detener termina la copia, y salir "   \
    "de esta página también: al volver siempre se empieza con la ventana vacía y el botón listo para iniciar de nuevo. Capturar cuesta a la estación algo de " \
    "memoria y no ralentiza nada más, pero no es una grabación: solo se muestra lo que llega mientras la ventana está abierta, y no se escribe nada en la "      \
    "flash."

/** @} */

#endif // LANG_ES_H
