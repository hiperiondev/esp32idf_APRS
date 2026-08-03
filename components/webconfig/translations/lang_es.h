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
#define TR_BRAND            "Administración Web esp32idf_APRS"
#define TR_LOGOUT           "Cerrar sesión"
#define TR_LOGGED_OUT_TITLE "Sesión cerrada"
#define TR_LOG_IN_AGAIN     "Iniciar sesión de nuevo"
#define TR_UNAUTHORIZED     "401 No autorizado"
#define TR_FORBIDDEN_CSRF   "403 Prohibido: no se pudo verificar el origen de la solicitud"
#define TR_SAVED_REDIRECT   "Guardado. Redirigiendo..."

/** @} */

/**
 * @name Sidebar menu
 * @{
 */
#define TR_MENU_DASHBOARD "Panel"
#define TR_MENU_MSGCHAT   "Env/Rec Msj"
#define TR_MENU_BULLETINS "Boletines"
#define TR_MENU_OBJITEMS  "Objetos e Ítems"
#define TR_MENU_STATION   "Estación"
#define TR_MENU_RADIO     "Radiomódem"
#define TR_MENU_MSG       "Mensaje"
#define TR_MENU_QUERY     "Consulta"
#define TR_MENU_IGATE     "IGate"
#define TR_MENU_DIGI      "Digirepetidor"
#define TR_MENU_TRACKER   "Rastreador"
#define TR_MENU_WX        "Meteorología"
#define TR_MENU_TLM       "Telemetría"
#define TR_MENU_SYSTEM    "Sistema"
#define TR_MENU_WIRELESS  "Inalámbrico"
#define TR_MENU_STORAGE   "Almacenamiento de archivos"
#define TR_MENU_ABOUT     "Firmware"

/** @} */

/**
 * @name Common buttons / widgets
 * @{
 */
#define TR_BTN_SAVE          "Guardar"
#define TR_BTN_AUTO_GENERATE "Generar automáticamente"
#define TR_BTN_LOOP_TEST     "PRUEBA DE BUCLE"
#define TR_LOOPTEST_SAVING   "Guardando..."
#define TR_LOOPTEST_RUNNING  "Probando..."
#define TR_LOOPTEST_FAILED   "Fallo en la solicitud"
#define TR_SHOW_PASSWORD     "Mostrar contraseña"

/** @} */

/**
 * @name page_about.c
 * @{
 */
#define TR_ABOUT_TITLE       "Firmware"
#define TR_ABOUT_FW_LEGEND   "Firmware"
#define TR_ABOUT_PROJECT     "Proyecto:"
#define TR_ABOUT_VERSION     "Versión:"
#define TR_ABOUT_BUILD_DATE  "Fecha de compilación:"
#define TR_ABOUT_IDF_VERSION "Versión de IDF:"
#define TR_ABOUT_PARTITION   "Partición en ejecución:"
#define TR_ABOUT_OTA_LEGEND  "Actualización OTA"
#define TR_ABOUT_OTA_BODY                                                                                                                                      \
    "Cargue un nuevo firmware .bin compilado para esta placa. Se escribe en la "                                                                               \
    "partición OTA inactiva mientras el dispositivo sigue funcionando con la "                                                                                 \
    "actual; el dispositivo solo cambia y se reinicia una vez que la carga "                                                                                   \
    "finaliza y se verifica. Si el nuevo firmware no arranca correctamente, "                                                                                  \
    "se revierte automáticamente en el próximo reinicio."
#define TR_OTA_TARGET_SLOT      "Partición destino:"
#define TR_OTA_SELECT_FILE      "Archivo de firmware (.bin):"
#define TR_OTA_UPLOAD_BTN       "Cargar y grabar"
#define TR_OTA_CONFIRM          "¿Cargar y grabar este firmware? El dispositivo se reiniciará al finalizar."
#define TR_OTA_NO_FILE_SELECTED "Seleccione primero un archivo de firmware .bin."
#define TR_OTA_UPLOADING        "Cargando y escribiendo en la memoria flash..."
#define TR_OTA_NO_PARTITION                                                                                                                                    \
    "No hay una partición OTA disponible en la tabla de particiones de este dispositivo. Vuelva a grabarlo una vez por USB/UART con el partitions.csv actual " \
    "para habilitar OTA."
#define TR_OTA_BEGIN_FAILED    "No se pudo iniciar la escritura OTA: "
#define TR_OTA_NO_FILE_CHOSEN  "no se recibió ningún archivo"
#define TR_OTA_UPLOAD_FAILED   "La carga del firmware falló"
#define TR_OTA_VALIDATE_FAILED "la validación de la imagen falló: el archivo no es una imagen de firmware válida para esta placa"
#define TR_OTA_SUCCESS         "Firmware escrito y verificado correctamente."
#define TR_OTA_REBOOTING       "Reiniciando con el nuevo firmware..."

/** @} */

/**
 * @name Common field/fieldset labels (auto-extracted from pages/<page>.c source files)
 * @{
 */
#define TR_F_ADD_TIMESTAMP                                 "Añadir marca de tiempo"
#define TR_F_ALTITUDE_M                                    "Altitud (m)"
#define TR_F_APRS_IS_SERVER                                "Servidor APRS-IS"
#define TR_F_APRS_MESSAGING                                "Mensajería APRS"
#define TR_F_APRS_PASSCODE                                 "Código de acceso APRS-IS"
#define TR_F_APRS_SYMBOLS                                  "Símbolos APRS"
#define TR_F_AUDIO_AFSK                                    "Audio / AFSK"
#define TR_F_ENABLE_AUDIO_MODEM                            "Activar módem ADC/DAC de audio"
#define TR_F_AFSK_MODULATION                               "Modulación"
#define TR_F_AUDIO_LOW_PASS_FILTER                         "Filtro paso bajo de audio"
#define TR_F_AUTO_WIDEN_N                                  "Automático (WIDEn-N)"
#define TR_F_BEACON_INTERVAL_S                             "Intervalo de baliza (s)"
#define TR_F_BEACON_POSITION                               "Baliza / Posición"
#define TR_F_BEACON_POSITION_2                             "Posición de baliza"
#define TR_F_BEACON_VIA_INTERNET                           "Baliza vía Internet"
#define TR_F_BEACON_VIA_RF                                 "Baliza vía RF"
#define TR_F_COMMENT                                       "Comentario"
#define TR_F_COMPRESS_POSITION                             "Comprimir posición"
#define TR_F_DASHBOARD                                     "Panel"
#define TR_F_DATA_INTERVAL_S                               "Intervalo de datos (s)"
#define TR_F_DIGIPEATER                                    "Digipeater"
#define TR_F_DIGI_DELAY_MS                                 "Retardo del digi (ms)"
#define TR_F_ENABLE                                        "Habilitar"
#define TR_F_ENABLE_DIGIPEATER                             "Habilitar Digipeater"
#define TR_F_ENABLE_IGATE                                  "Habilitar IGate"
#define TR_F_ENABLE_MESSAGING                              "Habilitar mensajería"
#define TR_F_ENABLE_TRACKER                                "Habilitar Tracker"
#define TR_F_ENABLE_WX                                     "Habilitar WX"
#define TR_F_FILE_STORAGE                                  "Almacenamiento de archivos"
#define TR_F_FILTER                                        "Filtro"
#define TR_F_FIXED_ALTITUDE_M                              "Altitud fija (m)"
#define TR_F_FIXED_INTERVAL_S                              "Intervalo fijo (s)"
#define TR_F_FIXED_LATITUDE                                "Latitud fija"
#define TR_F_FIXED_LONGITUDE                               "Longitud fija"
#define TR_F_FX_25_FORWARD_ERROR_CORRECTED_AX_25           "FX.25 (AX.25 con corrección de errores)"
#define TR_F_IGATE                                         "IGate"
#define TR_F_INCLUDE_ALTITUDE                              "Incluir altitud"
#define TR_F_INCLUDE_RSSI                                  "Incluir RSSI"
#define TR_F_INTERNET_TO_RF                                "Internet a RF"
#define TR_F_LATITUDE                                      "Latitud"
#define TR_F_LOG_TRACK                                     "Registrar recorrido"
#define TR_F_LONGITUDE                                     "Longitud"
#define TR_F_MESSAGE                                       "Mensaje"
#define TR_F_MESSAGE_ALARM_ENABLE                          "Activar alarma de mensajes"
#define TR_F_MESSAGE_ALARM_PIN                             "Pin de alarma de mensajes"
#define TR_F_MICE_POSITION                                 "Codificación de posición Mic-E"
#define TR_F_MODE                                          "Modo"
#define TR_F_MY_CALLSIGN                                   "Mi indicativo"
#define TR_USE_MY_STATION_DATA                             "Usar mis datos de estación"
#define TR_F_NAME                                          "Nombre"
#define TR_F_OBJECT_ITEM_NAME                              "Nombre del objeto/ítem"
#define TR_F_OBJECT_NAME                                   "Nombre del objeto"
#define TR_F_OPTIONS                                       "Opciones"
#define TR_F_PARM_UNIT_EQNS_INTERVAL_S                     "Intervalo PARM/UNIT/EQNS (s)"
#define TR_F_PASSWORD                                      "Contraseña"
#define TR_F_PHG                                           "PHG"
#define TR_F_POSITION                                      "Posición"
#define TR_F_PREAMBLE_MS                                   "Preámbulo (ms)"
#define TR_DISABLED                                        "Deshabilitado"
#define TR_GPIO_USED_BY                                    "GPIO%d (usado: %.30s)"
#define TR_F_PROTOCOL                                      "Protocolo"
#define TR_F_QUERY                                         "Consulta"
#define TR_F_ENABLE_QUERY                                  "Habilitar respondedor de consultas"
#define TR_F_QUERY_RF                                      "Responder consultas escuchadas en RF"
#define TR_F_QUERY_INET                                    "Responder consultas recibidas de APRS-IS"
#define TR_F_QUERY_APRS                                    "?APRS? - consulta general de estación"
#define TR_F_QUERY_WX                                      "?WX? - solicitud de reporte meteorológico"
#define TR_F_QUERY_IGATE                                   "?IGATE? - solicitud de estado del IGate"
#define TR_F_QUERY_DIRECTED                                "Consultas dirigidas (CALL:?query?)"
#define TR_F_QUERY_EXT                                     "Consultas dirigidas extendidas (?APRSD/?APRSH/?APRSM/?APRSO/?APRSP/?APRSS/?APRST)"
#define TR_F_QUERY_MIN_INTERVAL                            "Segundos mínimos entre respuestas idénticas"
#define TR_F_RADIO_MODEM                                   "Radiomódem"
#define TR_F_RETRY_COUNT                                   "Número de reintentos"
#define TR_F_RETRY_INTERVAL_S                              "Intervalo de reintento (s)"
#define TR_F_RF_TO_INTERNET                                "RF a Internet"
#define TR_F_RF_TX_BUFFERS                                 "Buffers de TX"
#define TR_F_PTT_MIN_UNKEY_MS                              "Tiempo mínimo de PTT liberado (ms)"
#define TR_F_CSMA_PERSISTENCE                              "Persistencia CSMA (p, 1-255)"
#define TR_F_SEND_RECEIVE_VIA_INTERNET                     "Enviar/recibir vía Internet"
#define TR_F_SEND_RECEIVE_VIA_RF                           "Enviar/recibir vía RF"
#define TR_F_SEND_VIA_INTERNET                             "Enviar vía Internet"
#define TR_F_SEND_VIA_RF                                   "Enviar vía RF"
#define TR_F_SENSOR_MAPPING_ENABLE_AVERAGED_SOURCE_CHANNEL "Mapeo de sensores (habilitar / promediado / canal de origen)"
#define TR_F_SERVER_HOST                                   "Host del servidor"
#define TR_F_SERVER_PORT                                   "Puerto del servidor"
#define TR_F_SSID                                          "SSID"
#define TR_F_STATION                                       "Estación"
#define TR_F_BULLETINS                                     "Boletines"
#define TR_F_BULLETIN_FMT                                  "Boletín %d"
#define TR_F_BULLETIN_ID                                   "Identificador (0-9 boletin, A-Z anuncio)"
#define TR_F_BULLETIN_GROUP                                "Grupo (hasta 5 caracteres, vacio = general)"
#define TR_F_BULLETIN_MSG                                  "Mensaje (máx 67 caract.)"
#define TR_F_BULLETIN_EXPIRE                               "Expira (horas, 0 = nunca)"
#define TR_F_OBJITEMS                                      "Objetos e Ítems"
#define TR_F_OBJITEM_FMT                                   "Objeto/Ítem %d"
#define TR_F_OBJITEM_TX_CONTROL                            "Control de Transmisión"
#define TR_F_OBJITEM_IDENTITY                              "Identidad y Estado"
#define TR_F_OBJITEM_POS_SYMBOL                            "Posición y Símbolo"
#define TR_F_OBJITEM_AREA_SECTION                          "Objeto de Área"
#define TR_F_OBJITEM_SIGNPOST_SECTION                      "Señal (Signpost)"
#define TR_F_OBJITEM_REPEATER_SECTION                      "Parámetros de Radio del Repetidor"
#define TR_F_OBJITEM_TIMING_SECTION                        "Temporización de Baliza"
#define TR_F_OBJITEM_TYPE                                  "Tipo"
#define TR_F_OBJITEM_TYPE_OBJECT                           "Objeto (con marca de tiempo)"
#define TR_F_OBJITEM_TYPE_ITEM                             "Ítem (permanente)"
#define TR_F_OBJITEM_ACTIVE                                "Activo (desmarcar = eliminar)"
#define TR_F_OBJITEM_SCOPE                                 "Alcance"
#define TR_F_OBJITEM_SCOPE_PRIVATE                         "Privado (no se transmite)"
#define TR_F_OBJITEM_SCOPE_LOCAL                           "Local (solo RF)"
#define TR_F_OBJITEM_SCOPE_GLOBAL                          "Global (RF + Internet)"
#define TR_F_OBJITEM_SYMBOL                                "Símbolo / superposición"
#define TR_F_OBJITEM_COURSE                                "Rumbo (grados, 0-359)"
#define TR_F_OBJITEM_SPEED                                 "Velocidad (nudos, 0 = omitir)"
#define TR_F_OBJITEM_AREA_SHAPE                            "Forma de área (símbolo \\l)"
#define TR_F_OBJITEM_SHAPE_CIRCLE                          "Círculo"
#define TR_F_OBJITEM_SHAPE_LINE                            "Línea"
#define TR_F_OBJITEM_SHAPE_ELLIPSE                         "Elipse"
#define TR_F_OBJITEM_SHAPE_TRIANGLE                        "Triángulo"
#define TR_F_OBJITEM_SHAPE_BOX                             "Caja"
#define TR_F_OBJITEM_SHAPE_FILLED                          " (rellena)"
#define TR_F_OBJITEM_AREA_COLOR                            "Color de área (0-15)"
#define TR_F_OBJITEM_AREA_LAT_OFF                          "Desplaz. latitud del área (grados)"
#define TR_F_OBJITEM_AREA_LON_OFF                          "Desplaz. longitud del área (grados)"
#define TR_F_OBJITEM_SIGNPOST                              "Texto de señal (símbolo \\m, 3 caract.)"
#define TR_F_OBJITEM_FREQ                                  "Frecuencia de monitoreo (MHz, 0 = ninguna)"
#define TR_F_OBJITEM_DUPLEX                                "Dirección dúplex"
#define TR_F_OBJITEM_DUPLEX_SIMPLEX                        "Símplex"
#define TR_F_OBJITEM_DUPLEX_PLUS                           "Positivo (+)"
#define TR_F_OBJITEM_DUPLEX_MINUS                          "Negativo (-)"
#define TR_F_OBJITEM_OFFSET                                "Desplazamiento dúplex (kHz)"
#define TR_F_OBJITEM_TONE                                  "Tono subaudible CTCSS (Hz, 0 = ninguno)"
#define TR_F_OBJITEM_PATH_FMT                              "Ruta %d"
#define TR_F_OBJITEM_QRU                                   "Membresía de grupo QRU"
#define TR_F_OBJITEM_QRU_NONE                              "(ninguno)"
#define TR_F_QRU_AMBU                                      "ambulancia"
#define TR_F_QRU_CLUB                                      "club de radioaficionados"
#define TR_F_QRU_ECHO                                      "Echolink"
#define TR_F_QRU_FIRE                                      "estación de bomberos"
#define TR_F_QRU_FOOD                                      "restaurantes"
#define TR_F_QRU_FUEL                                      "estaciones de servicio/gasolineras"
#define TR_F_QRU_HOSP                                      "hospitales"
#define TR_F_QRU_LIFEBOAT                                  "botes salvavidas"
#define TR_F_QRU_LTHS                                      "faros"
#define TR_F_QRU_POLI                                      "comisarías de policía"
#define TR_F_QRU_POST                                      "oficinas de correos"
#define TR_F_QRU_RD13                                      "repetidores D-Star 13cm"
#define TR_F_QRU_RD23                                      "repetidores D-Star 23cm"
#define TR_F_QRU_RD2M                                      "repetidores D-Star 2m"
#define TR_F_QRU_RD3C                                      "repetidores D-Star 3cm"
#define TR_F_QRU_RD70                                      "repetidores D-Star 70cm"
#define TR_F_QRU_RP10                                      "repetidores analógicos 10m"
#define TR_F_QRU_RP13                                      "repetidores analógicos 13cm"
#define TR_F_QRU_RP23                                      "repetidores analógicos 23cm"
#define TR_F_QRU_RP2M                                      "repetidores analógicos 2m"
#define TR_F_QRU_RP3C                                      "repetidores analógicos 3cm"
#define TR_F_QRU_RP6M                                      "repetidores analógicos 6m"
#define TR_F_QRU_RP70                                      "repetidores analógicos 70cm"
#define TR_F_QRU_RT13                                      "repetidores de televisión 13cm"
#define TR_F_QRU_RT23                                      "repetidores de televisión 23cm"
#define TR_F_QRU_RT3C                                      "repetidores de televisión 3cm"
#define TR_F_QRU_SRAIL                                     "ferrocarril de vapor"
#define TR_F_QRU_STOR                                      "tiendas de radioafición"
#define TR_F_QRU_T2SRV                                     "ubic. aprox. de servidores APRS-IS Tier 2"
#define TR_F_QRU_VETE                                      "veterinarios"
#define TR_F_QRU_WOTA                                      "Wainwrights On The Air"
#define TR_F_OBJITEM_INIT_RATE                             "Intervalo inicial (s)"
#define TR_F_OBJITEM_SLOW_RATE                             "Intervalo lento (s, 0 = sin decaimiento)"
#define TR_F_OBJITEM_DECAY                                 "Razón de decaimiento (ej. 2.0, <1 = ninguna)"
#define TR_NOTE_OBJITEM                                                                                                                                        \
    "Los Objetos llevan marca de tiempo (;NOMBRE); los Ítems son permanentes ()NOMBRE). Al desmarcar Activo se envían reportes de eliminación y luego se "     \
    "deshabilita automáticamente. El Alcance limita la transmisión independientemente de las casillas RF/Internet."
#define TR_F_STATUS_BEACON           "Baliza de estado"
#define TR_F_STATUS_INTERVAL_S_0_OFF "Intervalo de estado (s, 0=desactivado)"
#define TR_F_STATUS_TEXT             "Texto de estado"
#define TR_F_SYMBOL_IDLE             "Símbolo (inactivo)"
#define TR_F_SYMBOL_MOVING           "Símbolo (en movimiento)"
#define TR_F_SYMBOL_STOPPED          "Símbolo (detenido)"
#define TR_F_SYSTEM                  "Sistema"
#define TR_F_TELEMETRY               "Telemetría"
#define TR_F_BEACON                  "Baliza"
#define TR_F_TRACKER                 "Tracker"
#define TR_F_TX_TIME_SLOT_MS         "Intervalo de tiempo TX (ms)"
#define TR_F_UPLOAD                  "Subir"
#define TR_F_USERNAME                "Usuario"
#define TR_F_WEATHER                 "Clima"
#define TR_F_WEATHER_STATION         "Estación meteorológica"
#define TR_F_WIRELESS                "Inalámbrico"

#define TR_F_OFF "Apagado"

/** @} */

/**
 * @name page_common.c: dashboard / sysinfo
 * @{
 */
#define TR_ENABLED               "activado"
#define TR_DASH_DIGI_SHORT       "Digi"
#define TR_DASH_WX_SHORT         "WX:"
#define TR_DASH_UPTIME           "Tiempo activo:"
#define TR_DASH_FREE_HEAP        "Memoria libre:"
#define TR_DASH_LITTLEFS         "LittleFS:"
#define TR_DASH_SYSINFO          "Información del sistema"
#define TR_DASH_IGATE_TRAFFIC    "Tráfico IGate"
#define TR_TRAFFIC_PAUSE         "Pausar"
#define TR_TRAFFIC_RESUME        "Reanudar"
#define TR_TRAFFIC_CLEAR         "Limpiar"
#define TR_TRAFFIC_WAITING       "Esperando tráfico..."
#define TR_TRAFFIC_COL_TIME      "HORA"
#define TR_TRAFFIC_COL_TYPE      "TIPO"
#define TR_TRAFFIC_COL_DX        "DX"
#define TR_TRAFFIC_COL_PACKET    "PAQUETE"
#define TR_TRAFFIC_COL_AUDIO     "AUDIO"
#define TR_SYSINFO_CHIP          "Chip"
#define TR_SYSINFO_MODEL         "Modelo:"
#define TR_SYSINFO_CORES         "Núcleos:"
#define TR_SYSINFO_REVISION      "Revisión:"
#define TR_SYSINFO_CPU_FREQ      "Velocidad de CPU:"
#define TR_SYSINFO_CPU_FREQ_SET  "Establecer frecuencia de CPU"
#define TR_SYSINFO_CPU_FREQ_NOTE "Se guarda en la memoria flash y se vuelve a aplicar automáticamente en cada arranque."
#define TR_SYSINFO_FLASH_SIZE    "Tamaño de flash:"
#define TR_SYSINFO_MIN_FREE_HEAP "Memoria libre mínima:"
#define TR_DASH_REBOOT_REASON    "Motivo de reinicio:"

/** @} */

/**
 * @name page_common.c
 * @{
 */
#define TR_DASH_RADIO_INFO     "Info de Radio"
#define TR_DASH_MODEM          "MÓDEM"
#define TR_DASH_FX25           "FX.25"
#define TR_DASH_APRS_IS_SERVER "SERVIDOR APRS-IS"
#define TR_DASH_HOST           "HOST"
#define TR_DASH_PORT           "PUERTO"
#define TR_DASH_WIFI           "WiFi"
#define TR_DASH_MODE           "MODO"
#define TR_DASH_SSID           "SSID"
#define TR_DASH_RSSI           "RSSI"
#define TR_DASH_DISCONNECTED   "Desconectado"
#define TR_DASH_MODES_ENABLED  "Modos Activos"
#define TR_DASH_NETWORK_STATUS "Estado de Red"
#define TR_DASH_STATISTICS     "ESTADÍSTICAS"
#define TR_DASH_RADIO_RX       "RADIO RX:"
#define TR_DASH_PACKET_TX      "RADIO TX:"
#define TR_DASH_RF2INET        "RF2INET:"
#define TR_DASH_INET2RF        "INET2RF:"
#define TR_DASH_IGATE_RX       "IGATE RX:"
#define TR_DASH_IGATE_TX       "IGATE TX:"
#define TR_DASH_DIGI_STAT      "DIGI:"
#define TR_DASH_DROP_ERR       "DESCARTE/ERR:"
#define TR_DASH_DROP_BREAKDOWN "Detalle de Descartes"
#define TR_DASH_TX_QUEUE       "COLA TX RF:"
#define TR_DASH_LH_ICON        "ICONO"

/** @} */

/**
 * @name page_digi.c / page_igate.c / page_tracker.c telemetry notes
 * @{
 */
#define TR_NOTE_TLM_DIGI                                                                                                                                       \
    "La telemetría (EQNS/PARM/UNIT) para las balizas de Digi se configura en la "                                                                              \
    "página <a href='/tlm'>Telemetría</a>."
#define TR_NOTE_TLM_IGATE                                                                                                                                      \
    "La telemetría (EQNS/PARM/UNIT) para las balizas de IGate se configura en la "                                                                             \
    "página <a href='/tlm'>Telemetría</a>."
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
#define TR_F_SND_RCV_MSG      "Env/Rec Msj"
#define TR_MSGCHAT_MY_STATION "Mi Estación:"
#define TR_MSGCHAT_DISABLED_NOTE                                                                                                                               \
    "La Mensajería APRS está deshabilitada o no hay un indicativo configurado. Habilítela y configure un indicativo en la página Message primero."
#define TR_MSGCHAT_LOADING          "Cargando mensajes..."
#define TR_MSGCHAT_EMPTY            "Aún no hay mensajes."
#define TR_MSGCHAT_TO               "Para (indicativo):"
#define TR_MSGCHAT_TO_PLACEHOLDER   "N0CALL-9"
#define TR_MSGCHAT_TEXT             "Mensaje:"
#define TR_MSGCHAT_TEXT_PLACEHOLDER "Escriba un mensaje..."
#define TR_MSGCHAT_SEND             "Enviar"
#define TR_MSGCHAT_YOU              "Tú"
#define TR_MSGCHAT_ERR_EMPTY        "Ingrese un indicativo de destino y un mensaje."
#define TR_MSGCHAT_ERR_DISABLED     "La Mensajería APRS está deshabilitada en la página Message."
#define TR_MSGCHAT_ERR_NO_MYCALL    "No hay un indicativo de estación configurado."
#define TR_MSGCHAT_SENT_OK          "Enviado."
#define TR_MSGCHAT_SENT_FAIL        "Error al enviar."

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
#define TR_STORAGE_USAGE                 "Uso de LittleFS:"
#define TR_STORAGE_UPLOAD_FILE           "Subir archivo"
#define TR_STORAGE_CONFIRM_FORMAT        "¿Borrar TODOS los archivos y restablecer la configuración de fábrica?"
#define TR_STORAGE_FORMAT_BTN            "Formatear LittleFS"
#define TR_STORAGE_SIZE_BYTES            "Tamaño (bytes)"
#define TR_STORAGE_ACTIONS               "Acciones"
#define TR_STORAGE_DOWNLOAD              "Descargar"
#define TR_STORAGE_CONFIRM_DELETE_PREFIX "¿Eliminar "
#define TR_STORAGE_DELETE                "Eliminar"
#define TR_STORAGE_UPLOAD_OK             "Subido:"
#define TR_STORAGE_UPLOAD_FAILED         "La carga falló. Verifique el archivo y que haya espacio libre suficiente."
#define TR_STORAGE_NO_FILE_CHOSEN        "Seleccione primero un archivo."
#define TR_STORAGE_BACK                  "Volver"

/** @} */

/**
 * @name page_symbol.c
 * @{
 */
#define TR_SYM_HOUSE_HF        "Casa (HF)"
#define TR_SYM_CAR             "Automóvil"
#define TR_SYM_MOTORCYCLE      "Motocicleta"
#define TR_SYM_BICYCLE         "Bicicleta"
#define TR_SYM_TRUCK           "Camión"
#define TR_SYM_VAN             "Furgoneta"
#define TR_SYM_JEEP            "Jeep"
#define TR_SYM_FIRE_TRUCK      "Camión de bomberos"
#define TR_SYM_POLICE          "Policía"
#define TR_SYM_HOUSE           "Casa"
#define TR_SYM_DIGIPEATER      "Digipeater"
#define TR_SYM_GATEWAY         "Puerta de enlace"
#define TR_SYM_WEATHER_STATION "Estación meteorológica"
#define TR_SYM_BALLOON         "Globo"
#define TR_SYM_SPACE_SHUTTLE   "Transbordador espacial"
#define TR_SYM_SAILBOAT        "Velero"
#define TR_SYM_NWS_SITE        "Sitio NWS"
#define TR_SYM_TCP_IP          "TCP/IP"
#define TR_SYM_CAR_ALT         "Automóvil (tabla alterna)"
#define TR_SYM_WX_STATION_ALT  "Estación WX (tabla alterna)"
#define TR_SYM_INTRO                                                                                                                                           \
    "Referencia rápida de los códigos de símbolo APRS más comunes. Cada página de "                                                                            \
    "servicio (IGate / Digi / Tracker) tiene su propio campo de símbolo de texto libre "                                                                       \
    "\u2014 copie el código de 2 caracteres desde aquí a ese campo."
#define TR_SYM_CODE                 "Código"
#define TR_SYM_MEANING              "Significado"
#define TR_SYM_CURRENTLY_CONFIGURED "Símbolos actualmente configurados"

/** @} */

/**
 * @name page_system.c
 * @{
 */
#define TR_SYS_WEB_ADMIN_LOGIN       "Acceso al administrador web"
#define TR_SYS_DEVICE                "Dispositivo"
#define TR_SYS_HOST_NAME             "Nombre de host"
#define TR_SYS_TIME_ZONE             "Zona horaria (desfase UTC)"
#define TR_SYS_SYNC_NTP              "Sincronizar hora vía NTP"
#define TR_SYS_NTP_HOST              "Host NTP (primario)"
#define TR_SYS_NTP_HOST2             "Host NTP (alternativo 2)"
#define TR_SYS_NTP_HOST3             "Host NTP (alternativo 3)"
#define TR_SYS_NTP_RESYNC            "Intervalo de resincronización NTP (s, mín 30)"
#define TR_SYS_AUTO_RESET_TIMEOUT    "Tiempo de reinicio automático (min, 0=desactivado)"
#define TR_SYS_DIGI_PATH_ALIASES     "Alias de ruta del Digipeater"
#define TR_SYS_PATH_1                "Ruta 1"
#define TR_SYS_PATH_2                "Ruta 2"
#define TR_SYS_PATH_3                "Ruta 3"
#define TR_SYS_PATH_4                "Ruta 4"
#define TR_SYS_CONFIRM_FACTORY_RESET "¿Restablecer TODA la configuración a los valores de fábrica?"
#define TR_SYS_FACTORY_RESET         "Restablecer a fábrica"

/** @} */

/**
 * @name page_tlm.c
 * @{
 */
#define TR_TLM_AVG "Prom"
#define TR_TLM_BIT "Bit"
/** @} */

/**
 * @name page_tlm.c: telemetry configurator
 * @{
 */
#define TR_TLM_ENABLE_TELEMETRY   "Habilitar telemetría"
#define TR_TLM_REPORT_PARAMS      "Parámetros del reporte"
#define TR_TLM_PATH_DIGIS         "Ruta (digipeaters)"
#define TR_TLM_DESTINATION        "Destino"
#define TR_TLM_AUTO_INC_SEQ       "Autoincrementar secuencia"
#define TR_TLM_ANALOG_FIELD_WIDTH "Ancho de campo analógico"
#define TR_TLM_FIELDW_3DIGIT      "3 dígitos con ceros (000-255, estricto)"
#define TR_TLM_FIELDW_AUTO        "Mínimo / según necesidad (enteros o decimales)"
#define TR_TLM_OMIT_TRAILING      "Omitir canales finales sin usar"
#define TR_TLM_TRAIL_COMMENT      "Comentario final (opcional, tras los bits)"
#define TR_TLM_ANALOG_COUNT       "Canales analógicos enviados"
#define TR_TLM_DIGITAL_COUNT      "Bits digitales enviados"
#define TR_TLM_DEF_MESSAGES       "Mensajes de definición"
#define TR_TLM_GEN_PARM           "PARM - nombres de canal y bits"
#define TR_TLM_GEN_UNIT           "UNIT - unidades / etiquetas de estado de bit"
#define TR_TLM_GEN_EQNS           "EQNS - coeficientes de escala (A,B,C)"
#define TR_TLM_GEN_BITS           "BITS - sentido de bit + nombre"
#define TR_TLM_ANALOG_LEGEND      "Canales analógicos (A1-A5)"
#define TR_TLM_DIGITAL_LEGEND     "Canales digitales (B1-B8)"
#define TR_TLM_UNIT               "Unidad"
#define TR_TLM_SOURCE             "Fuente"
#define TR_TLM_RF                 "RF"
#define TR_TLM_RAW_MIN            "Bruto mín"
#define TR_TLM_RAW_MAX            "Bruto máx"
#define TR_TLM_COEF_A             "A (cuadrático)"
#define TR_TLM_COEF_B             "B (lineal / pendiente)"
#define TR_TLM_COEF_C             "C (desplazamiento)"
#define TR_TLM_DECIMALS           "Decimales mostrados"
#define TR_TLM_ON_STATE           "Significado de estado activo"
#define TR_TLM_SENSE              "Sentido"
#define TR_TLM_LABEL              "Etiqueta"
#define TR_TLM_CALIB_WIZARD       "Asistente de calibracion de 2 puntos"
#define TR_TLM_CALIB_PROMPT_X1    "Lectura cruda #1 (x1):"
#define TR_TLM_CALIB_PROMPT_Y1    "Valor real conocido en x1:"
#define TR_TLM_CALIB_PROMPT_X2    "Lectura cruda #2 (x2):"
#define TR_TLM_CALIB_PROMPT_Y2    "Valor real conocido en x2:"
#define TR_TLM_CALIB_SAME_X       "x1 y x2 deben ser diferentes."
#define TR_TLM_CALIB_CANCELLED    "Calibracion cancelada: ingrese valores numericos."

/** @} */

/**
 * @name page_radio.c
 * @{
 */
#define TR_RADIO_AUDIO_HW_TITLE "Hardware de audio (en tiempo de compilación)"
#define TR_RADIO_AUDIO_HW_INFO                                                                                                                                 \
    "<br>DAC salida: GPIO%d<br>ADC entrada: GPIO%d<br>Pin PTT: %s<br>PTT activo en alto: %s<br>Atenuación ADC: %d<br>ADC: %d Hz<br>DAC: %d Hz"
#define TR_RADIO_AUDIO_HW_NOTE ""

/** @} */

/**
 * @name page_wireless.c
 * @{
 */
#define TR_WIFI_MODE_LEGEND      "Modo WiFi"
#define TR_WIFI_STATION          "Estación (STA)"
#define TR_WIFI_ACCESS_POINT     "Punto de acceso (AP)"
#define TR_WIFI_AP_STA           "AP + STA"
#define TR_WIFI_TX_POWER         "Potencia TX (0-20 dBm)"
#define TR_WIFI_AP_SSID          "SSID del AP"
#define TR_WIFI_AP_PASSWORD      "Contraseña del AP"
#define TR_WIFI_AP_CHANNEL       "Canal del AP"
#define TR_WIFI_CLIENT_LEGEND    "Cliente WiFi n.º %d"
#define TR_BTN_WIFI_SCAN         "ESCANEAR WIFI"
#define TR_WIFI_SSID_PLACEHOLDER "Nombre de la red (escríbalo o use Buscar WiFi)"
#define TR_WIFI_STA_NEEDS_SSID                                                                                                                                 \
    "Guardado, pero NO se conectará: el Modo selecciona estación, pero ningún bloque Cliente WiFi tiene 'Habilitar' marcado y un SSID cargado. Corríjalo y "   \
    "guarde de nuevo."
#define TR_WIFI_SCANNING    "Escaneando..."
#define TR_WIFI_SCAN_FAILED "Error al escanear"

/** @} */

/**
 * @name page_wx.c
 * @{
 */
#define TR_WX_WIND_SPEED     "Velocidad del viento"
#define TR_WX_WIND_GUST      "Ráfaga de viento"
#define TR_WX_WIND_DIRECTION "Dirección del viento"
#define TR_WX_TEMPERATURE    "Temperatura"
#define TR_WX_RAIN_1H        "Lluvia 1h"
#define TR_WX_RAIN_24H       "Lluvia 24h"
#define TR_WX_RAIN_MIDNIGHT  "Lluvia desde medianoche"
#define TR_WX_HUMIDITY       "Humedad"
#define TR_WX_PRESSURE       "Presión"
#define TR_WX_LUMINOSITY     "Luminosidad"
#define TR_WX_SNOW           "Nieve"
#define TR_WX_FLOOD_FT       "Nivel de crecida (ft)"
#define TR_WX_FLOOD_M        "Nivel de crecida"
#define TR_WX_FIELD          "Campo WX"
#define TR_WX_CHANNEL        "Canal"
#define TR_WX_CHANNEL_NONE   "(ninguno)"
#define TR_WX_VALUE          "Valor"

/** @} */

/**
 * @name IGATE page additions (station symbol, path preset, timestamp, PHG, filters)
 * @{
 */
#define TR_F_STATION_SYMBOL    "Símbolo de Estación"
#define TR_F_SYMBOL_TABLE      "Tabla"
#define TR_F_SYMBOL_CODE       "Símbolo"
#define TR_BTN_PICK_SYMBOL     "..."
#define TR_SYM_PICK_HINT       "Haga clic en el icono para elegir el símbolo"
#define TR_F_PATH              "RUTA"
#define TR_PATH_DIRECT         "Directo (sin ruta)"
#define TR_PATH_CUSTOM_UNSET   "(no configurado)"
#define TR_PATH_DIRECT_HINT    "sin ruta de digipeater - solo lo recibirán estaciones que lo escuchen directamente"
#define TR_PATH_HOP_HINT       "salto(s) vía digipeater, codificado como sufijo SSID (forma corta WIDEn-N)"
#define TR_PATH_CUSTOM_HINT    "ruta de digipeater personalizada configurada en la página Sistema"
#define TR_F_TIME_STAMP        "Marca de Tiempo"
#define TR_F_TX_CHANNEL        "Canal TX"
#define TR_F_PHG_SECTION       "PHG"
#define TR_F_ENABLE_PHG        "Habilitar PHG"
#define TR_F_RADIO_TX_POWER    "Potencia TX de Radio"
#define TR_F_ANTENNA_GAIN      "Ganancia de Antena"
#define TR_F_HEIGHT_M          "Altura (m)"
#define TR_F_ANTENNA_DIRECTION "Antena/Dirección"
#define TR_F_PHG_TEXT          "Texto PHG"
#define TR_F_EXT_SECTION       "Extension de datos"
#define TR_F_ENABLE_EXT        "Habilitar extension de datos"
#define TR_F_EXT_TYPE          "Tipo de extension"
#define TR_EXT_PHG             "PHG - potencia/altura/ganancia/directividad"
#define TR_EXT_RNG             "RNG - alcance de radio precalculado"
#define TR_EXT_DFS             "DFS - intensidad de senal omni-DF"
#define TR_F_EXT_RANGE_MI      "Alcance de radio (millas)"
#define TR_F_EXT_DFS_STRENGTH  "Intensidad de senal (puntos S, 0 = no se oye)"
#define TR_F_POS_AMBIGUITY     "Ambiguedad de posicion"
#define TR_AMB_NONE            "Precision total"
#define TR_AMB_TENTH           "Al 1/10 de minuto"
#define TR_AMB_MINUTE          "Al minuto"
#define TR_AMB_TEN_MINUTES     "A los 10 minutos"
#define TR_AMB_DEGREE          "Al grado"
#define TR_F_STATUS_GRID       "Localizador Maidenhead en los reportes de estado"
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
#define TR_F_FILTER_RF2INET    "Filtro RF a Internet"
#define TR_F_FILTER_INET2RF    "Filtro Internet a RF"
#define TR_FILT_MESSAGE        "Mensaje"
#define TR_FILT_STATUS         "Estado"
#define TR_FILT_TELEMETRY      "Telemetría"
#define TR_FILT_WEATHER        "Clima"
#define TR_FILT_OBJECT         "Objeto"
#define TR_FILT_ITEM           "Ítem"
#define TR_FILT_QUERY          "Consulta"
#define TR_FILT_BUOY           "Boya"
#define TR_FILT_POSITION       "Posición"

#define TR_F_CALLSIGN_FILTER      "Filtro de Indicativos"
#define TR_F_BUDLIST_MODE_RF2INET "Modo RF a Internet"
#define TR_F_BUDLIST_MODE_INET2RF "Modo Internet a RF"
#define TR_BUDLIST_OFF            "Desactivado"
#define TR_BUDLIST_WHITELIST      "Lista Blanca"
#define TR_BUDLIST_BLACKLIST      "Lista Negra"
#define TR_F_BUDLIST_CALL         "Indicativo"
#define TR_NOTE_BUDLIST                                                                                                                                        \
    "Lista de indicativos compartida, hasta 8 entradas. Lista Blanca: solo pasan los indicativos listados. Lista Negra: los indicativos listados se bloquean."

#define TR_F_RANGE_FILTER_EN  "Activar filtro de distancia"
#define TR_F_RANGE_KM         "Distancia máxima (km, 0 = sin límite)"
#define TR_F_PREFIX_FILTER_EN "Activar filtro de prefijo de indicativo"
#define TR_F_PREFIXES         "Prefijos permitidos (separados por coma)"
#define TR_NOTE_RANGE_PREFIX                                                                                                                                   \
    "Filtro local aplicado solo a RF -> Internet, independiente del filtro por tipo de carga anterior. La distancia se mide desde la posición de Mi "          \
    "Estación; los paquetes cuya posición no se puede decodificar no se ven afectados por el filtro de distancia."

#define TR_F_3RDPARTY_UNWRAP_EN "Retransmitir tráfico de terceros (}) en lista blanca"
#define TR_NOTE_3RDPARTY_UNWRAP                                                                                                                                \
    "Desactivado por defecto. Solo tiene efecto cuando el Filtro de Indicativos de Internet a RF anterior está en modo Lista Blanca: un paquete envuelto de "  \
    "terceros solo se desenvuelve y retransmite si su indicativo de origen interno está en la lista blanca. Active esto solo si confía en la fuente "          \
    "específica y la ha incluido en la lista blanca - volver a filtrar tráfico de terceros sin esta restricción es la causa más común de bucles de IGate."

#define TR_F_SATGATE      "Lista de Satélites Digipetidores"
#define TR_F_SATGATE_CALL "Indicativo de Satélite"
#define TR_NOTE_SATGATE                                                                                                                                        \
    "Indicativos de satélites/ISS digipetidores (p. ej. ISS, PSAT). Un paquete enrutado por uno de estos solo se retransmite a APRS-IS si la entrada de "      \
    "trayectoria del digipetidor está realmente marcada como usada. Hasta 8 entradas; deje una entrada vacía para desactivarla."

#define TR_F_DUP_CACHE            "Supresión de Duplicados"
#define TR_F_DUP_CACHE_SIZE       "Tamaño de Caché (entradas)"
#define TR_F_DUP_CACHE_TIMEOUT_MS "Ventana de Supresión (ms)"
#define TR_NOTE_DUP_CACHE                                                                                                                                      \
    "Compartido por el IGate y el Digipetidor para suprimir copias repetidas del mismo paquete. Un digipetidor ocupado en una frecuencia congestionada "       \
    "puede necesitar una caché más grande; un IGate rural con poco tráfico puede preferir una ventana más corta."

#define TR_SYM_ICON            "Icono"
#define TR_SYM_QUICK_PICK      "Selección Rápida"
#define TR_SYM_PRIMARY_TABLE   "Tabla Primaria ( / )"
#define TR_SYM_ALTERNATE_TABLE "Tabla Alternativa ( \\ )"
#define TR_SYM_TRACKER_IDLE    "Tracker (inactivo):"
#define TR_SYM_TRACKER_MOVE    "Tracker (movimiento):"
#define TR_SYM_TRACKER_STOP    "Tracker (detenido):"

/** @} */

#endif // LANG_ES_H
