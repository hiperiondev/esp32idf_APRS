/**
 * @file lang_en.h
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
 * @brief English strings.
 *
 * Only ever included by translations.h when LANGUAGE == LANG_EN. Keep this file's
 * TR_xxx macro list identical (same names) across all lang_xx.h files - only the
 * string contents should differ.
 */

#ifndef LANG_EN_H
#define LANG_EN_H

/**
 * @name Brand / chrome
 * @{
 */
#define TR_BRAND            "esp32idf_APRS Web Admin"
#define TR_LOGOUT           "Logout"
#define TR_LOGGED_OUT_TITLE "Logged out"
#define TR_LOG_IN_AGAIN     "Log in again"
#define TR_UNAUTHORIZED     "401 Unauthorized"
#define TR_FORBIDDEN_CSRF   "403 Forbidden: request origin could not be verified"
#define TR_SAVED_REDIRECT   "Saved. Redirecting..."

/** @} */

/**
 * @name Sidebar menu
 * @{
 */
#define TR_MENU_DASHBOARD "Dashboard"
#define TR_MENU_MSGCHAT   "Snd/Rcv Msg"
#define TR_MENU_BULLETINS "Bulletins"
#define TR_MENU_OBJITEMS  "Objects and Items"
#define TR_MENU_STATION   "Station"
#define TR_MENU_RADIO     "Radiomodem"
#define TR_MENU_MSG       "Message"
#define TR_MENU_QUERY     "Query"
#define TR_MENU_IGATE     "IGate"
#define TR_MENU_DIGI      "Digipeater"
#define TR_MENU_TRACKER   "Tracker"
#define TR_MENU_WX        "Weather"
#define TR_MENU_TLM       "Telemetry"
#define TR_MENU_SYSTEM    "System"
#define TR_MENU_WIRELESS  "Wireless"
#define TR_MENU_STORAGE   "File Storage"
#define TR_MENU_ABOUT     "Firmware"

/** @} */

/**
 * @name Common buttons / widgets
 * @{
 */
#define TR_BTN_SAVE          "Save"
#define TR_BTN_AUTO_GENERATE "Auto Generate"
#define TR_BTN_LOOP_TEST     "LOOP TEST"
#define TR_LOOPTEST_SAVING   "Saving settings..."
#define TR_LOOPTEST_RUNNING  "Testing..."
#define TR_LOOPTEST_FAILED   "Request failed"
#define TR_SHOW_PASSWORD     "Show password"

/** @} */

/**
 * @name page_about.c
 * @{
 */
#define TR_ABOUT_TITLE       "Firmware"
#define TR_ABOUT_FW_LEGEND   "Firmware"
#define TR_ABOUT_PROJECT     "Project:"
#define TR_ABOUT_VERSION     "Version:"
#define TR_ABOUT_BUILD_DATE  "Build date:"
#define TR_ABOUT_IDF_VERSION "IDF version:"
#define TR_ABOUT_PARTITION   "Running partition:"
#define TR_ABOUT_OTA_LEGEND  "OTA Update"
#define TR_ABOUT_OTA_BODY                                                                                                                                      \
    "Upload a new firmware .bin built for this board. It is written to the "                                                                                   \
    "inactive OTA slot while the device keeps running from the current one; "                                                                                  \
    "the device only switches over and reboots once the upload is complete "                                                                                   \
    "and verified. If the new firmware fails to boot cleanly, it is rolled "                                                                                   \
    "back automatically on the next restart."
#define TR_OTA_TARGET_SLOT      "Target slot:"
#define TR_OTA_SELECT_FILE      "Firmware file (.bin):"
#define TR_OTA_UPLOAD_BTN       "Upload &amp; Flash"
#define TR_OTA_CONFIRM          "Upload and flash this firmware? The device will reboot when done."
#define TR_OTA_NO_FILE_SELECTED "Choose a firmware .bin file first."
#define TR_OTA_UPLOADING        "Uploading and writing to flash..."
#define TR_OTA_NO_PARTITION                                                                                                                                    \
    "No OTA update slot is available on this device's partition table. Reflash it once over USB/UART with the current partitions.csv to enable OTA."
#define TR_OTA_BEGIN_FAILED    "Could not start the OTA write: "
#define TR_OTA_NO_FILE_CHOSEN  "no file was received"
#define TR_OTA_UPLOAD_FAILED   "Firmware upload failed"
#define TR_OTA_VALIDATE_FAILED "image validation failed - the file is not a valid firmware image for this board"
#define TR_OTA_SUCCESS         "Firmware written and verified successfully."
#define TR_OTA_REBOOTING       "Rebooting into the new firmware now..."

/** @} */

/**
 * @name Common field/fieldset labels (auto-extracted from pages/<page>.c source files)
 * @{
 */
#define TR_F_ADD_TIMESTAMP                                 "Add timestamp"
#define TR_F_ALTITUDE_M                                    "Altitude (m)"
#define TR_F_APRS_IS_SERVER                                "APRS-IS Server"
#define TR_F_APRS_MESSAGING                                "APRS Messaging"
#define TR_F_APRS_PASSCODE                                 "APRS-IS Passcode"
#define TR_F_APRS_SYMBOLS                                  "APRS Symbols"
#define TR_F_AUDIO_AFSK                                    "Audio / AFSK"
#define TR_F_ENABLE_AUDIO_MODEM                            "Enable audio ADC/DAC modem"
#define TR_F_AFSK_MODULATION                               "Modulation"
#define TR_F_AUDIO_LOW_PASS_FILTER                         "Audio low-pass filter"
#define TR_F_AUTO_WIDEN_N                                  "Auto (WIDEn-N)"
#define TR_F_BEACON_INTERVAL_S                             "Beacon interval (s)"
#define TR_F_BEACON_POSITION                               "Beacon / Position"
#define TR_F_BEACON_POSITION_2                             "Beacon position"
#define TR_F_BEACON_VIA_INTERNET                           "Beacon via Internet"
#define TR_F_BEACON_VIA_RF                                 "Beacon via RF"
#define TR_F_COMMENT                                       "Comment"
#define TR_F_COMPRESS_POSITION                             "Compress position"
#define TR_F_DASHBOARD                                     "Dashboard"
#define TR_F_DATA_INTERVAL_S                               "Data interval (s)"
#define TR_F_DIGIPEATER                                    "Digipeater"
#define TR_F_DIGI_DELAY_MS                                 "Digi delay (ms)"
#define TR_F_DUPE_FILTER_WINDOW_S                          "Dupe filter window (s)"
#define TR_F_ENABLE                                        "Enable"
#define TR_F_ENABLE_DIGIPEATER                             "Enable Digipeater"
#define TR_F_ENABLE_IGATE                                  "Enable IGate"
#define TR_F_ENABLE_MESSAGING                              "Enable messaging"
#define TR_F_ENABLE_TRACKER                                "Enable Tracker"
#define TR_F_ENABLE_WX                                     "Enable WX"
#define TR_F_FILE_STORAGE                                  "File Storage"
#define TR_F_FILTER                                        "Filter"
#define TR_F_FIXED_ALTITUDE_M                              "Fixed Altitude (m)"
#define TR_F_FIXED_INTERVAL_S                              "Fixed interval (s)"
#define TR_F_FIXED_LATITUDE                                "Fixed Latitude"
#define TR_F_FIXED_LONGITUDE                               "Fixed Longitude"
#define TR_F_FX_25_FORWARD_ERROR_CORRECTED_AX_25           "FX.25 (forward-error-corrected AX.25)"
#define TR_F_IGATE                                         "IGate"
#define TR_F_INCLUDE_ALTITUDE                              "Include altitude"
#define TR_F_INCLUDE_RSSI                                  "Include RSSI"
#define TR_F_INTERNET_TO_RF                                "Internet to RF"
#define TR_F_LATITUDE                                      "Latitude"
#define TR_F_LOG_TRACK                                     "Log track"
#define TR_F_LONGITUDE                                     "Longitude"
#define TR_F_MESSAGE                                       "Message"
#define TR_F_MESSAGE_ALARM_ENABLE                          "Enable Message Alarm"
#define TR_F_MESSAGE_ALARM_PIN                             "Message Alarm pin"
#define TR_F_MICE_POSITION                                 "Mic-E position encoding"
#define TR_F_MODE                                          "Mode"
#define TR_F_MY_CALLSIGN                                   "My Callsign"
#define TR_USE_MY_STATION_DATA                             "Use My Station Data"
#define TR_F_NAME                                          "Name"
#define TR_F_OBJECT_ITEM_NAME                              "Object/Item name"
#define TR_F_OBJECT_NAME                                   "Object name"
#define TR_F_OPTIONS                                       "Options"
#define TR_F_PARM_UNIT_EQNS_INTERVAL_S                     "PARM/UNIT/EQNS interval (s)"
#define TR_F_PASSWORD                                      "Password"
#define TR_F_PHG                                           "PHG"
#define TR_F_POSITION                                      "Position"
#define TR_F_PREAMBLE_MS                                   "Preamble (ms)"
#define TR_DISABLED                                        "Disabled"
#define TR_GPIO_USED_BY                                    "GPIO%d (used: %.30s)"
#define TR_F_PROTOCOL                                      "Protocol"
#define TR_F_QUERY                                         "Query"
#define TR_F_ENABLE_QUERY                                  "Enable query responder"
#define TR_F_QUERY_RF                                      "RF"
#define TR_F_QUERY_INET                                    "APRS-IS"
#define TR_F_QUERY_APRS                                    "?APRS? - general station query"
#define TR_F_QUERY_WX                                      "?WX? - weather report request"
#define TR_F_QUERY_IGATE                                   "?IGATE? - IGate status request"
#define TR_F_QUERY_DIRECTED                                "Directed queries (CALL:?query?)"
#define TR_F_QUERY_EXT                                     "Extended directed queries (?APRSD/?APRSH/?APRSM/?APRSO/?APRSP/?APRSS/?APRST)"
#define TR_F_QUERY_MIN_INTERVAL                            "Minimum seconds between identical responses"
#define TR_F_RADIO_MODEM                                   "Radiomodem"
#define TR_F_RETRY_COUNT                                   "Retry count"
#define TR_F_RETRY_INTERVAL_S                              "Retry interval (s)"
#define TR_F_RF_TO_INTERNET                                "RF to Internet"
#define TR_F_RF_TX_BUFFERS                                 "TX buffers"
#define TR_F_PTT_MIN_UNKEY_MS                              "PTT minimum unkey time (ms)"
#define TR_F_CSMA_PERSISTENCE                              "CSMA persistence (p, 1-255)"
#define TR_F_SEND_RECEIVE_VIA_INTERNET                     "Send/receive via Internet"
#define TR_F_SEND_RECEIVE_VIA_RF                           "Send/receive via RF"
#define TR_F_SEND_VIA_INTERNET                             "Send via Internet"
#define TR_F_SEND_VIA_RF                                   "Send via RF"
#define TR_F_SENSOR_MAPPING_ENABLE_AVERAGED_SOURCE_CHANNEL "Sensor Mapping (enable / averaged / source channel)"
#define TR_F_SERVER_HOST                                   "Server Host"
#define TR_F_SERVER_PORT                                   "Server Port"
#define TR_F_SSID                                          "SSID"
#define TR_F_STATION                                       "Station"
#define TR_F_BULLETINS                                     "Bulletins"
#define TR_F_BULLETIN_FMT                                  "Bulletin %d"
#define TR_F_BULLETIN_ID                                   "Identifier (0-9 bulletin, A-Z announcement)"
#define TR_F_BULLETIN_GROUP                                "Group (up to 5 chars, empty = general)"
#define TR_F_BULLETIN_MSG                                  "Message (max 67 chars)"
#define TR_F_BULLETIN_EXPIRE                               "Expire (hours, 0 = never)"
#define TR_F_OBJITEMS                                      "Objects and Items"
#define TR_F_OBJITEM_FMT                                   "Object/Item %d"
#define TR_F_OBJITEM_TX_CONTROL                            "Transmission Control"
#define TR_F_OBJITEM_IDENTITY                              "Identity & State"
#define TR_F_OBJITEM_POS_SYMBOL                            "Position & Symbol"
#define TR_F_OBJITEM_AREA_SECTION                          "Area Object"
#define TR_F_OBJITEM_SIGNPOST_SECTION                      "Signpost"
#define TR_F_OBJITEM_REPEATER_SECTION                      "Repeater Radio Parameters"
#define TR_F_OBJITEM_TIMING_SECTION                        "Beacon Timing"
#define TR_F_OBJITEM_TYPE                                  "Type"
#define TR_F_OBJITEM_TYPE_OBJECT                           "Object (timestamped)"
#define TR_F_OBJITEM_TYPE_ITEM                             "Item (permanent)"
#define TR_F_OBJITEM_ACTIVE                                "Active (uncheck = kill)"
#define TR_F_OBJITEM_SCOPE                                 "Scope"
#define TR_F_OBJITEM_SCOPE_PRIVATE                         "Private (not transmitted)"
#define TR_F_OBJITEM_SCOPE_LOCAL                           "Local (RF only)"
#define TR_F_OBJITEM_SCOPE_GLOBAL                          "Global (RF + Internet)"
#define TR_F_OBJITEM_SYMBOL                                "Symbol / overlay"
#define TR_F_OBJITEM_COURSE                                "Course (deg, 0-359)"
#define TR_F_OBJITEM_SPEED                                 "Speed (knots, 0 = omit)"
#define TR_F_OBJITEM_AREA_SHAPE                            "Area shape (\\l symbol)"
#define TR_F_OBJITEM_SHAPE_CIRCLE                          "Circle"
#define TR_F_OBJITEM_SHAPE_LINE                            "Line"
#define TR_F_OBJITEM_SHAPE_ELLIPSE                         "Ellipse"
#define TR_F_OBJITEM_SHAPE_TRIANGLE                        "Triangle"
#define TR_F_OBJITEM_SHAPE_BOX                             "Box"
#define TR_F_OBJITEM_SHAPE_FILLED                          " (filled)"
#define TR_F_OBJITEM_AREA_COLOR                            "Area color (0-15)"
#define TR_F_OBJITEM_AREA_LAT_OFF                          "Area latitude offset (deg)"
#define TR_F_OBJITEM_AREA_LON_OFF                          "Area longitude offset (deg)"
#define TR_F_OBJITEM_SIGNPOST                              "Signpost text (\\m symbol, 3 chars)"
#define TR_F_OBJITEM_FREQ                                  "Monitor frequency (MHz, 0 = none)"
#define TR_F_OBJITEM_DUPLEX                                "Duplex direction"
#define TR_F_OBJITEM_DUPLEX_SIMPLEX                        "Simplex"
#define TR_F_OBJITEM_DUPLEX_PLUS                           "Plus (+)"
#define TR_F_OBJITEM_DUPLEX_MINUS                          "Minus (-)"
#define TR_F_OBJITEM_OFFSET                                "Duplex offset (kHz)"
#define TR_F_OBJITEM_TONE                                  "Subaudible tone CTCSS (Hz, 0 = none)"
#define TR_F_OBJITEM_PATH_FMT                              "Path %d"
#define TR_F_OBJITEM_QRU                                   "QRU group membership"
#define TR_F_OBJITEM_QRU_NONE                              "(none)"
#define TR_F_QRU_AMBU                                      "ambulance"
#define TR_F_QRU_CLUB                                      "ham radio club"
#define TR_F_QRU_ECHO                                      "Echolink"
#define TR_F_QRU_FIRE                                      "fire station"
#define TR_F_QRU_FOOD                                      "restaurants"
#define TR_F_QRU_FUEL                                      "gas/petrol stations"
#define TR_F_QRU_HOSP                                      "hospitals"
#define TR_F_QRU_LIFEBOAT                                  "lifeboats"
#define TR_F_QRU_LTHS                                      "lighthouses"
#define TR_F_QRU_POLI                                      "police stations"
#define TR_F_QRU_POST                                      "post offices"
#define TR_F_QRU_RD13                                      "D-Star 13cm repeaters"
#define TR_F_QRU_RD23                                      "D-Star 23cm repeaters"
#define TR_F_QRU_RD2M                                      "D-Star 2m repeaters"
#define TR_F_QRU_RD3C                                      "D-Star 3cm repeaters"
#define TR_F_QRU_RD70                                      "D-Star 70cm repeaters"
#define TR_F_QRU_RP10                                      "analog 10m repeaters"
#define TR_F_QRU_RP13                                      "analog 13cm repeaters"
#define TR_F_QRU_RP23                                      "analog 23cm repeaters"
#define TR_F_QRU_RP2M                                      "analog 2m repeaters"
#define TR_F_QRU_RP3C                                      "analog 3cm repeaters"
#define TR_F_QRU_RP6M                                      "analog 6m repeaters"
#define TR_F_QRU_RP70                                      "analog 70cm repeaters"
#define TR_F_QRU_RT13                                      "television 13cm repeaters"
#define TR_F_QRU_RT23                                      "television 23cm repeaters"
#define TR_F_QRU_RT3C                                      "television 3cm repeaters"
#define TR_F_QRU_SRAIL                                     "steam railroad"
#define TR_F_QRU_STOR                                      "Amateur Radio stores"
#define TR_F_QRU_T2SRV                                     "approx. locations of Tier 2 APRS-IS servers"
#define TR_F_QRU_VETE                                      "veterinarians"
#define TR_F_QRU_WOTA                                      "Wainwrights On The Air"
#define TR_F_OBJITEM_INIT_RATE                             "Initial repeat rate (s)"
#define TR_F_OBJITEM_SLOW_RATE                             "Slow repeat rate (s, 0 = no decay)"
#define TR_F_OBJITEM_DECAY                                 "Decay ratio (e.g. 2.0, <1 = none)"
#define TR_NOTE_OBJITEM                                                                                                                                        \
    "Objects are timestamped (;NAME); Items are permanent ()NAME). Unchecking Active sends kill reports, then auto-disables. Scope limits transmission "       \
    "regardless of the RF/Internet checks."
#define TR_F_STATUS_BEACON           "Status Beacon"
#define TR_F_STATUS_INTERVAL_S_0_OFF "Status interval (s, 0=off)"
#define TR_F_STATUS_TEXT             "Status text"
#define TR_F_SYMBOL_IDLE             "Symbol (idle)"
#define TR_F_SYMBOL_MOVING           "Symbol (moving)"
#define TR_F_SYMBOL_STOPPED          "Symbol (stopped)"
#define TR_F_SYSTEM                  "System"
#define TR_F_TELEMETRY               "Telemetry"
#define TR_F_BEACON                  "Beacon"
#define TR_F_TRACKER                 "Tracker"
#define TR_F_TX_TIME_SLOT_MS         "TX time-slot (ms)"
#define TR_F_UPLOAD                  "Upload"
#define TR_F_USERNAME                "Username"
#define TR_F_WEATHER                 "Weather"
#define TR_F_WEATHER_STATION         "Weather Station"
#define TR_F_WIRELESS                "Wireless"

#define TR_F_OFF "Off"

/** @} */

/**
 * @name page_common.c: dashboard / sysinfo
 * @{
 */
#define TR_ENABLED               "enabled"
#define TR_DASH_DIGI_SHORT       "Digi"
#define TR_DASH_WX_SHORT         "WX:"
#define TR_DASH_UPTIME           "Uptime:"
#define TR_DASH_FREE_HEAP        "Free heap:"
#define TR_DASH_LITTLEFS         "LittleFS:"
#define TR_DASH_SYSINFO          "System Info"
#define TR_DASH_IGATE_TRAFFIC    "IGate Traffic"
#define TR_TRAFFIC_PAUSE         "Pause"
#define TR_TRAFFIC_RESUME        "Resume"
#define TR_TRAFFIC_CLEAR         "Clear"
#define TR_TRAFFIC_WAITING       "Waiting for traffic..."
#define TR_TRAFFIC_COL_TIME      "TIME"
#define TR_TRAFFIC_COL_TYPE      "TYPE"
#define TR_TRAFFIC_COL_DX        "DX"
#define TR_TRAFFIC_COL_PACKET    "PACKET"
#define TR_TRAFFIC_COL_AUDIO     "AUDIO"
#define TR_SYSINFO_CHIP          "Chip"
#define TR_SYSINFO_MODEL         "Model:"
#define TR_SYSINFO_CORES         "Cores:"
#define TR_SYSINFO_REVISION      "Revision:"
#define TR_SYSINFO_CPU_FREQ      "CPU speed:"
#define TR_SYSINFO_CPU_FREQ_SET  "Set CPU frequency"
#define TR_SYSINFO_CPU_FREQ_NOTE "Saved to flash and re-applied automatically on every boot."
#define TR_SYSINFO_FLASH_SIZE    "Flash size:"
#define TR_SYSINFO_MIN_FREE_HEAP "Min free heap:"
#define TR_DASH_REBOOT_REASON    "Reboot Reason:"

/** @} */

/**
 * @name page_common.c
 * @{
 */
#define TR_DASH_RADIO_INFO     "Radio Info"
#define TR_DASH_MODEM          "MODEM"
#define TR_DASH_FX25           "FX.25"
#define TR_DASH_APRS_IS_SERVER "APRS-IS SERVER"
#define TR_DASH_HOST           "HOST"
#define TR_DASH_PORT           "PORT"
#define TR_DASH_WIFI           "WiFi"
#define TR_DASH_MODE           "MODE"
#define TR_DASH_SSID           "SSID"
#define TR_DASH_RSSI           "RSSI"
#define TR_DASH_DISCONNECTED   "Disconnect"
#define TR_DASH_MODES_ENABLED  "Modes Enabled"
#define TR_DASH_NETWORK_STATUS "Network Status"
#define TR_DASH_STATISTICS     "STATISTICS"
#define TR_DASH_RADIO_RX       "RADIO RX:"
#define TR_DASH_PACKET_TX      "RADIO TX:"
#define TR_DASH_RF2INET        "RF2INET:"
#define TR_DASH_INET2RF        "INET2RF:"
#define TR_DASH_IGATE_RX       "IGATE RX:"
#define TR_DASH_IGATE_TX       "IGATE TX:"
#define TR_DASH_DIGI_STAT      "DIGI:"
#define TR_DASH_DROP_ERR       "DROP/ERR:"
#define TR_DASH_DROP_BREAKDOWN "Drop Breakdown"
#define TR_DASH_TX_QUEUE       "RF TX QUEUE:"
#define TR_DASH_LH_ICON        "ICON"

/** @} */

/**
 * @name page_digi.c / page_igate.c / page_tracker.c telemetry notes
 * @{
 */
#define TR_NOTE_TLM_DIGI                                                                                                                                       \
    "Telemetry (EQNS/PARM/UNIT) for Digi beacons is configured on the "                                                                                        \
    "<a href='/tlm'>Telemetry</a> page."
#define TR_NOTE_TLM_IGATE                                                                                                                                      \
    "Telemetry (EQNS/PARM/UNIT) for IGate beacons is configured on the "                                                                                       \
    "<a href='/tlm'>Telemetry</a> page."
#define TR_NOTE_TLM_TRACKER                                                                                                                                    \
    "Telemetry (EQNS/PARM/UNIT) for Tracker beacons is configured on the "                                                                                     \
    "<a href='/tlm'>Telemetry</a> page."

/** @} */

/**
 * @name page_msgchat.c ("Snd/Rcv Msg")
 * @{
 */
#define TR_F_SND_RCV_MSG            "Snd/Rcv Msg"
#define TR_MSGCHAT_MY_STATION       "My Station:"
#define TR_MSGCHAT_DISABLED_NOTE    "APRS Messaging is disabled or no station callsign is configured. Enable it and set a callsign on the Message page first."
#define TR_MSGCHAT_LOADING          "Loading messages..."
#define TR_MSGCHAT_EMPTY            "No messages yet."
#define TR_MSGCHAT_TO               "To (callsign):"
#define TR_MSGCHAT_TO_PLACEHOLDER   "N0CALL-9"
#define TR_MSGCHAT_TEXT             "Message:"
#define TR_MSGCHAT_TEXT_PLACEHOLDER "Type a message..."
#define TR_MSGCHAT_SEND             "Send"
#define TR_MSGCHAT_YOU              "You"
#define TR_MSGCHAT_ERR_EMPTY        "Enter a destination callsign and a message."
#define TR_MSGCHAT_ERR_DISABLED     "APRS Messaging is disabled on the Message page."
#define TR_MSGCHAT_ERR_NO_MYCALL    "No station callsign configured."
#define TR_MSGCHAT_SENT_OK          "Sent."
#define TR_MSGCHAT_SENT_FAIL        "Send failed."

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
#define TR_STORAGE_USAGE                 "LittleFS usage:"
#define TR_STORAGE_UPLOAD_FILE           "Upload file"
#define TR_STORAGE_CONFIRM_FORMAT        "Erase ALL files and reset config to defaults?"
#define TR_STORAGE_FORMAT_BTN            "Format LittleFS"
#define TR_STORAGE_SIZE_BYTES            "Size (bytes)"
#define TR_STORAGE_ACTIONS               "Actions"
#define TR_STORAGE_DOWNLOAD              "Download"
#define TR_STORAGE_CONFIRM_DELETE_PREFIX "Delete "
#define TR_STORAGE_DELETE                "Delete"
#define TR_STORAGE_UPLOAD_OK             "Uploaded:"
#define TR_STORAGE_UPLOAD_FAILED         "Upload failed. Check the file and make sure there is enough free space."
#define TR_STORAGE_NO_FILE_CHOSEN        "Choose a file first."
#define TR_STORAGE_BACK                  "Back"

/** @} */

/**
 * @name page_symbol.c
 * @{
 */
#define TR_SYM_HOUSE_HF        "House (HF)"
#define TR_SYM_CAR             "Car"
#define TR_SYM_MOTORCYCLE      "Motorcycle"
#define TR_SYM_BICYCLE         "Bicycle"
#define TR_SYM_TRUCK           "Truck"
#define TR_SYM_VAN             "Van"
#define TR_SYM_JEEP            "Jeep"
#define TR_SYM_FIRE_TRUCK      "Fire truck"
#define TR_SYM_POLICE          "Police"
#define TR_SYM_HOUSE           "House"
#define TR_SYM_DIGIPEATER      "Digipeater"
#define TR_SYM_GATEWAY         "Gateway"
#define TR_SYM_WEATHER_STATION "Weather station"
#define TR_SYM_BALLOON         "Balloon"
#define TR_SYM_SPACE_SHUTTLE   "Space shuttle"
#define TR_SYM_SAILBOAT        "Sailboat"
#define TR_SYM_NWS_SITE        "NWS site"
#define TR_SYM_TCP_IP          "TCP/IP"
#define TR_SYM_CAR_ALT         "Car (alternate table)"
#define TR_SYM_WX_STATION_ALT  "WX station (alternate table)"
#define TR_SYM_INTRO                                                                                                                                           \
    "Quick reference for common APRS symbol codes. Each service page "                                                                                         \
    "(IGate / Digi / Tracker) has its own free-text symbol field \u2014 copy the "                                                                             \
    "2-character code from here into that field."
#define TR_SYM_CODE                 "Code"
#define TR_SYM_MEANING              "Meaning"
#define TR_SYM_CURRENTLY_CONFIGURED "Currently configured symbols"

/** @} */

/**
 * @name page_system.c
 * @{
 */
#define TR_SYS_WEB_ADMIN_LOGIN       "Web Admin Login"
#define TR_SYS_DEVICE                "Device"
#define TR_SYS_HOST_NAME             "Host name"
#define TR_SYS_TIME_ZONE             "Time zone (UTC offset)"
#define TR_SYS_SYNC_NTP              "Sync time via NTP"
#define TR_SYS_NTP_HOST              "NTP host (primary)"
#define TR_SYS_NTP_HOST2             "NTP host (fallback 2)"
#define TR_SYS_NTP_HOST3             "NTP host (fallback 3)"
#define TR_SYS_NTP_RESYNC            "NTP resync interval (s, min 30)"
#define TR_SYS_AUTO_RESET_TIMEOUT    "Auto-reset timeout (min, 0=off)"
#define TR_SYS_DIGI_PATH_ALIASES     "Digipeater Path Aliases"
#define TR_SYS_PATH_1                "Path 1"
#define TR_SYS_PATH_2                "Path 2"
#define TR_SYS_PATH_3                "Path 3"
#define TR_SYS_PATH_4                "Path 4"
#define TR_SYS_CONFIRM_FACTORY_RESET "Reset ALL settings to factory defaults?"
#define TR_SYS_FACTORY_RESET         "Factory Reset"

/** @} */

/**
 * @name page_tlm.c
 * @{
 */
#define TR_TLM_AVG "Avg"
#define TR_TLM_BIT "Bit"
/** @} */

/**
 * @name page_tlm.c: telemetry configurator
 * @{
 */
#define TR_TLM_ENABLE_TELEMETRY   "Enable Telemetry"
#define TR_TLM_REPORT_PARAMS      "Report Parameters"
#define TR_TLM_PATH_DIGIS         "Path (digipeaters)"
#define TR_TLM_DESTINATION        "Destination"
#define TR_TLM_AUTO_INC_SEQ       "Auto-increment sequence"
#define TR_TLM_ANALOG_FIELD_WIDTH "Analog field width"
#define TR_TLM_FIELDW_3DIGIT      "3-digit zero-padded (000-255, strict)"
#define TR_TLM_FIELDW_AUTO        "Minimal / as-needed (integers or decimals)"
#define TR_TLM_OMIT_TRAILING      "Omit unused trailing channels"
#define TR_TLM_TRAIL_COMMENT      "Trailing comment (optional, after bits)"
#define TR_TLM_ANALOG_COUNT       "Analog channels sent"
#define TR_TLM_DIGITAL_COUNT      "Digital bits sent"
#define TR_TLM_DEF_MESSAGES       "Definition Messages"
#define TR_TLM_GEN_PARM           "PARM - channel & bit names"
#define TR_TLM_GEN_UNIT           "UNIT - units / bit-state labels"
#define TR_TLM_GEN_EQNS           "EQNS - scaling coefficients (A,B,C)"
#define TR_TLM_GEN_BITS           "BITS - bit sense + name"
#define TR_TLM_ANALOG_LEGEND      "Analog Channels (A1-A5)"
#define TR_TLM_DIGITAL_LEGEND     "Digital Channels (B1-B8)"
#define TR_TLM_UNIT               "Unit"
#define TR_TLM_SOURCE             "Source"
#define TR_TLM_RF                 "RF"
#define TR_TLM_RAW_MIN            "Raw min"
#define TR_TLM_RAW_MAX            "Raw max"
#define TR_TLM_COEF_A             "A (quadratic)"
#define TR_TLM_COEF_B             "B (linear / slope)"
#define TR_TLM_COEF_C             "C (offset)"
#define TR_TLM_DECIMALS           "Displayed decimals"
#define TR_TLM_ON_STATE           "On-state means"
#define TR_TLM_SENSE              "Sense"
#define TR_TLM_LABEL              "Label"
#define TR_TLM_CALIB_WIZARD       "2-point calibration wizard"
#define TR_TLM_CALIB_PROMPT_X1    "Raw reading #1 (x1):"
#define TR_TLM_CALIB_PROMPT_Y1    "Known real-world value at x1:"
#define TR_TLM_CALIB_PROMPT_X2    "Raw reading #2 (x2):"
#define TR_TLM_CALIB_PROMPT_Y2    "Known real-world value at x2:"
#define TR_TLM_CALIB_SAME_X       "x1 and x2 must differ."
#define TR_TLM_CALIB_CANCELLED    "Calibration cancelled: enter numeric values."

/** @} */

/**
 * @name page_radio.c
 * @{
 */
#define TR_RADIO_AUDIO_HW_TITLE "Audio hardware (compile-time)"
#define TR_RADIO_AUDIO_HW_INFO  "<br>DAC out: GPIO%d<br>ADC in: GPIO%d<br>PTT pin: %s<br>PTT active-high: %s<br>ADC attenuation: %d<br>ADC: %d Hz<br>DAC: %d Hz"
#define TR_RADIO_AUDIO_HW_NOTE  ""

/** @} */

/**
 * @name page_wireless.c
 * @{
 */
#define TR_WIFI_MODE_LEGEND      "WiFi Mode"
#define TR_WIFI_STATION          "Station (STA)"
#define TR_WIFI_ACCESS_POINT     "Access Point (AP)"
#define TR_WIFI_AP_STA           "AP + STA"
#define TR_WIFI_TX_POWER         "TX Power (0-20 dBm)"
#define TR_WIFI_AP_SSID          "AP SSID"
#define TR_WIFI_AP_PASSWORD      "AP Password"
#define TR_WIFI_AP_CHANNEL       "AP Channel"
#define TR_WIFI_CLIENT_LEGEND    "WiFi Client #%d"
#define TR_BTN_WIFI_SCAN         "WIFI SCAN"
#define TR_WIFI_SSID_PLACEHOLDER "Network name (type it, or use WiFi Scan)"
#define TR_WIFI_STA_NEEDS_SSID                                                                                                                                 \
    "Saved, but this will NOT connect: Mode selects a station, yet no WiFi Client block has both 'Enable' ticked and an SSID filled in. Fix that and Save "    \
    "again."
#define TR_WIFI_SCANNING    "Scanning..."
#define TR_WIFI_SCAN_FAILED "Scan failed"

/** @} */

/**
 * @name page_wx.c
 * @{
 */
#define TR_WX_WIND_SPEED     "Wind Speed"
#define TR_WX_WIND_GUST      "Wind Gust"
#define TR_WX_WIND_DIRECTION "Wind Direction"
#define TR_WX_TEMPERATURE    "Temperature"
#define TR_WX_RAIN_1H        "Rain 1h"
#define TR_WX_RAIN_24H       "Rain 24h"
#define TR_WX_RAIN_MIDNIGHT  "Rain since midnight"
#define TR_WX_HUMIDITY       "Humidity"
#define TR_WX_PRESSURE       "Pressure"
#define TR_WX_LUMINOSITY     "Luminosity"
#define TR_WX_SNOW           "Snow"
#define TR_WX_FLOOD_FT       "Flood Height (ft)"
#define TR_WX_FLOOD_M        "Flood Height"
#define TR_WX_FIELD          "WX Field"
#define TR_WX_CHANNEL        "Channel"
#define TR_WX_CHANNEL_NONE   "(none)"
#define TR_WX_VALUE          "Value"

/** @} */

/**
 * @name IGATE page additions (station symbol, path preset, timestamp, PHG, filters)
 * @{
 */
#define TR_F_STATION_SYMBOL    "Station Symbol"
#define TR_F_SYMBOL_TABLE      "Table"
#define TR_F_SYMBOL_CODE       "Symbol"
#define TR_BTN_PICK_SYMBOL     "..."
#define TR_SYM_PICK_HINT       "Click icon for select symbol"
#define TR_F_PATH              "PATH"
#define TR_PATH_DIRECT         "Direct (no path)"
#define TR_PATH_CUSTOM_UNSET   "(not set)"
#define TR_PATH_DIRECT_HINT    "no digipeater path - only stations that hear you directly will receive it"
#define TR_PATH_HOP_HINT       "hop(s) via digipeater, encoded as an SSID suffix (short WIDEn-N form)"
#define TR_PATH_CUSTOM_HINT    "custom digipeater path configured on the System page"
#define TR_F_TIME_STAMP        "Time Stamp"
#define TR_F_TX_CHANNEL        "TX Channel"
#define TR_F_PHG_SECTION       "PHG"
#define TR_F_ENABLE_PHG        "Enable PHG"
#define TR_F_RADIO_TX_POWER    "Radio TX Power"
#define TR_F_ANTENNA_GAIN      "Antenna Gain"
#define TR_F_HEIGHT_M          "Height (m)"
#define TR_F_ANTENNA_DIRECTION "Antenna/Direction"
#define TR_F_PHG_TEXT          "PHG Text"
#define TR_F_EXT_SECTION       "Data Extension"
#define TR_F_ENABLE_EXT        "Enable data extension"
#define TR_F_EXT_TYPE          "Extension type"
#define TR_EXT_PHG             "PHG - power/height/gain/directivity"
#define TR_EXT_RNG             "RNG - pre-calculated radio range"
#define TR_EXT_DFS             "DFS - omni-DF signal strength"
#define TR_F_EXT_RANGE_MI      "Radio range (miles)"
#define TR_F_EXT_DFS_STRENGTH  "Signal strength (S-points, 0 = not heard)"
#define TR_F_POS_AMBIGUITY     "Position ambiguity"
#define TR_AMB_NONE            "Full precision"
#define TR_AMB_TENTH           "Nearest 1/10 minute"
#define TR_AMB_MINUTE          "Nearest minute"
#define TR_AMB_TEN_MINUTES     "Nearest 10 minutes"
#define TR_AMB_DEGREE          "Nearest degree"
#define TR_F_STATUS_GRID       "Maidenhead locator in status reports"
#define TR_DIR_OMNI            "Omni"
#define TR_DIR_N               "N"
#define TR_DIR_NE              "NE"
#define TR_DIR_E               "E"
#define TR_DIR_SE              "SE"
#define TR_DIR_S               "S"
#define TR_DIR_SW              "SW"
#define TR_DIR_W               "W"
#define TR_DIR_NW              "NW"
#define TR_F_IGATE_FILTER      "IGate Filter"
#define TR_F_FILTER_RF2INET    "Filter RF to Internet"
#define TR_F_FILTER_INET2RF    "Filter Internet to RF"
#define TR_FILT_MESSAGE        "Message"
#define TR_FILT_STATUS         "Status"
#define TR_FILT_TELEMETRY      "Telemetry"
#define TR_FILT_WEATHER        "Weather"
#define TR_FILT_OBJECT         "Object"
#define TR_FILT_ITEM           "Item"
#define TR_FILT_QUERY          "Query"
#define TR_FILT_BUOY           "Buoy"
#define TR_FILT_POSITION       "Position"

#define TR_F_CALLSIGN_FILTER      "Callsign Filter"
#define TR_F_BUDLIST_MODE_RF2INET "RF to Internet Mode"
#define TR_F_BUDLIST_MODE_INET2RF "Internet to RF Mode"
#define TR_BUDLIST_OFF            "Off"
#define TR_BUDLIST_WHITELIST      "Whitelist"
#define TR_BUDLIST_BLACKLIST      "Blacklist"
#define TR_F_BUDLIST_CALL         "Callsign"
#define TR_NOTE_BUDLIST           "Shared callsign list, up to 8 entries. Whitelist: only listed calls pass. Blacklist: listed calls are blocked."

#define TR_F_RANGE_FILTER_EN  "Enable range filter"
#define TR_F_RANGE_KM         "Max distance (km, 0 = unlimited)"
#define TR_F_PREFIX_FILTER_EN "Enable callsign-prefix filter"
#define TR_F_PREFIXES         "Allowed prefixes (comma-separated)"
#define TR_NOTE_RANGE_PREFIX                                                                                                                                   \
    "Local gate applied only to RF -> Internet, independent of the payload-type filter above. Range is measured from My Station's position; packets whose "    \
    "position can't be decoded are not affected by the range filter."

#define TR_F_3RDPARTY_UNWRAP_EN "Relay whitelisted third-party (}) traffic"
#define TR_NOTE_3RDPARTY_UNWRAP                                                                                                                                \
    "Off by default. Only takes effect when the Internet to RF Callsign Filter above is set to Whitelist: a third-party-wrapped packet is only ever "          \
    "unwrapped and relayed if its inner source callsign is itself on the whitelist. Only enable if you trust and have whitelisted the specific source - "      \
    "re-gating third-party traffic without this restriction is the most common cause of IGate loops."

#define TR_F_SATGATE      "Satellite Gate List"
#define TR_F_SATGATE_CALL "Satellite Callsign"
#define TR_NOTE_SATGATE                                                                                                                                        \
    "Callsigns of satellite/ISS digipeaters (e.g. ISS, PSAT). A frame routed through one of these is only gated to APRS-IS if the digipeater's path entry "    \
    "is actually marked used. Up to 8 entries; leave a slot blank to disable it."

#define TR_F_DUP_CACHE            "Duplicate Suppression"
#define TR_F_DUP_CACHE_SIZE       "Cache Size (entries)"
#define TR_F_DUP_CACHE_TIMEOUT_MS "Suppression Window (ms)"
#define TR_NOTE_DUP_CACHE                                                                                                                                      \
    "Shared by the IGate and the Digipeater to suppress repeated copies of the same frame. A busy digipeater on a congested frequency may need a larger "      \
    "cache; a sparse rural IGate may prefer a shorter window."

#define TR_SYM_ICON            "Icon"
#define TR_SYM_QUICK_PICK      "Quick Pick"
#define TR_SYM_PRIMARY_TABLE   "Primary Table ( / )"
#define TR_SYM_ALTERNATE_TABLE "Alternate Table ( \\ )"
#define TR_SYM_TRACKER_IDLE    "Tracker (idle):"
#define TR_SYM_TRACKER_MOVE    "Tracker (move):"
#define TR_SYM_TRACKER_STOP    "Tracker (stop):"

/** @} */

#endif // LANG_EN_H
