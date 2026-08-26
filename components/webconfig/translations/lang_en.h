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
/** Product name shown in the page header and browser title bar. */
#define TR_BRAND "esp32idf_APRS Web Admin"
/** Caption of the log-out link in the page header. */
#define TR_LOGOUT "Logout"
/** Heading of the page shown after a successful log-out. */
#define TR_LOGGED_OUT_TITLE "Logged out"
/** Caption of the link back to the login prompt after logging out. */
#define TR_LOG_IN_AGAIN "Log in again"
/** Body of the HTTP 401 response sent when authentication fails. */
#define TR_UNAUTHORIZED "401 Unauthorized"
/** Body of the HTTP 403 response sent when a POST fails the cross-site request check. */
#define TR_FORBIDDEN_CSRF "403 Forbidden: request origin could not be verified"
/** Interstitial shown after a successful save, while the browser is redirected back to the form. */
#define TR_SAVED_REDIRECT "Saved. Redirecting..."
/** Warning shown when settings were accepted but could not be committed to flash. */
#define TR_SAVE_FAILED "Save failed: the settings could not be written to flash. They are still in effect until the next restart."

/** @} */

/**
 * @name Sidebar menu
 * @{
 */
/** Sidebar navigation entry for the Dashboard page, rendered on the sidebar. */
#define TR_MENU_DASHBOARD "Dashboard"
/** Sidebar navigation entry for the Send/Receive Message page, rendered on the sidebar. */
#define TR_MENU_MSGCHAT "Snd/Rcv Msg"
/** Sidebar navigation entry for the Bulletins page, rendered on the sidebar. */
#define TR_MENU_BULLETINS "Bulletins"
/** Sidebar navigation entry for the Objects and Items page, rendered on the sidebar. */
#define TR_MENU_OBJITEMS "Objects and Items"
/** Sidebar navigation entry for the Station page, rendered on the sidebar. */
#define TR_MENU_STATION "Station"
/** Sidebar navigation entry for the Radiomodem page, rendered on the sidebar. */
#define TR_MENU_RADIO "Radiomodem"
/** Sidebar navigation entry for the Message page, rendered on the sidebar. */
#define TR_MENU_MSG "Message"
/** Sidebar navigation entry for the Query page, rendered on the sidebar. */
#define TR_MENU_QUERY "Query"
/** Sidebar navigation entry for the IGate page, rendered on the sidebar. */
#define TR_MENU_IGATE "IGate"
/** Sidebar navigation entry for the Digipeater page, rendered on the sidebar. */
#define TR_MENU_DIGI "Digipeater"
/** Sidebar navigation entry for the Tracker page, rendered on the sidebar. */
#define TR_MENU_TRACKER "Tracker"
/** Sidebar navigation entry for the Weather page, rendered on the sidebar. */
#define TR_MENU_WX "Weather"
/** Sidebar navigation entry for the Telemetry page, rendered on the sidebar. */
#define TR_MENU_TLM "Telemetry"
/** Sidebar navigation entry for the GPS page, rendered on the sidebar. */
#define TR_MENU_GPS "GPS"
/** Sidebar navigation entry for the Telegram page, rendered on the sidebar. */
#define TR_MENU_TELEGRAM "Telegram"
/** Sidebar navigation entry for the System page, rendered on the sidebar. */
#define TR_MENU_SYSTEM "System"
/** Sidebar navigation entry for the Wireless page, rendered on the sidebar. */
#define TR_MENU_WIRELESS "Wireless"
/** Sidebar navigation entry for the File Storage page, rendered on the sidebar. */
#define TR_MENU_STORAGE "File Storage"
/** Sidebar navigation entry for the Firmware page, rendered on the sidebar. */
#define TR_MENU_ABOUT "Firmware"

/** @} */

/**
 * @name Common buttons / widgets
 * @{
 */
/** Caption of the "save" button, rendered on any page. */
#define TR_BTN_SAVE "Save"
/** Caption of the "auto generate" button, rendered on any page. */
#define TR_BTN_AUTO_GENERATE "Auto Generate"
/** Caption of the "loop test" button, rendered on any page. */
#define TR_BTN_LOOP_TEST "LOOP TEST"
/** Loopback-test status message: saving, rendered on any page. */
#define TR_LOOPTEST_SAVING "Saving settings..."
/** Loopback-test status message: running, rendered on any page. */
#define TR_LOOPTEST_RUNNING "Testing..."
/** Loopback-test status message: failed, rendered on any page. */
#define TR_LOOPTEST_FAILED "Request failed"
/** Caption of the checkbox that reveals a masked password field. */
#define TR_SHOW_PASSWORD "Show password"

/** @} */

/**
 * @name page_about.c
 * @{
 */
/** Firmware page label for title, rendered on the Firmware page. */
#define TR_ABOUT_TITLE "Firmware"
/** Firmware page label for fw legend, rendered on the Firmware page. */
#define TR_ABOUT_FW_LEGEND "Firmware"
/** Firmware page label for project, rendered on the Firmware page. */
#define TR_ABOUT_PROJECT "Project:"
/** Firmware page label for version, rendered on the Firmware page. */
#define TR_ABOUT_VERSION "Version:"
/** Firmware page label for build date, rendered on the Firmware page. */
#define TR_ABOUT_BUILD_DATE "Build date:"
/** Firmware page label for idf version, rendered on the Firmware page. */
#define TR_ABOUT_IDF_VERSION "IDF version:"
/** Firmware page label for partition, rendered on the Firmware page. */
#define TR_ABOUT_PARTITION "Running partition:"
/** Firmware page label for ota legend, rendered on the Firmware page. */
#define TR_ABOUT_OTA_LEGEND "OTA Update"
/** Firmware page label for ota body, rendered on the Firmware page. */
#define TR_ABOUT_OTA_BODY                                                                                                                                      \
    "Upload a new firmware .bin built for this board. It is written to the "                                                                                   \
    "inactive OTA slot while the device keeps running from the current one; "                                                                                  \
    "the device only switches over and reboots once the upload is complete "                                                                                   \
    "and verified. If the new firmware fails to boot cleanly, it is rolled "                                                                                   \
    "back automatically on the next restart."
/** Firmware-update label or status message for target slot, rendered on the Firmware page. */
#define TR_OTA_TARGET_SLOT "Target slot:"
/** Firmware-update label or status message for select file, rendered on the Firmware page. */
#define TR_OTA_SELECT_FILE "Firmware file (.bin):"
/** Firmware-update label or status message for upload btn, rendered on the Firmware page. */
#define TR_OTA_UPLOAD_BTN "Upload &amp; Flash"
/** Firmware-update label or status message for confirm, rendered on the Firmware page. */
#define TR_OTA_CONFIRM "Upload and flash this firmware? The device will reboot when done."
/** Firmware-update label or status message for no file selected, rendered on the Firmware page. */
#define TR_OTA_NO_FILE_SELECTED "Choose a firmware .bin file first."
/** Firmware-update label or status message for uploading, rendered on the Firmware page. */
#define TR_OTA_UPLOADING "Uploading and writing to flash..."
/** Firmware-update label or status message for no partition, rendered on the Firmware page. */
#define TR_OTA_NO_PARTITION                                                                                                                                    \
    "No OTA update slot is available on this device's partition table. Reflash it once over USB/UART with the current partitions.csv to enable OTA."
/** Firmware-update label or status message for begin failed, rendered on the Firmware page. */
#define TR_OTA_BEGIN_FAILED "Could not start the OTA write: "
/** Firmware-update label or status message for no file chosen, rendered on the Firmware page. */
#define TR_OTA_NO_FILE_CHOSEN "no file was received"
/** Firmware-update label or status message for upload failed, rendered on the Firmware page. */
#define TR_OTA_UPLOAD_FAILED "Firmware upload failed"
/** Firmware-update label or status message for validate failed, rendered on the Firmware page. */
#define TR_OTA_VALIDATE_FAILED "image validation failed - the file is not a valid firmware image for this board"
/** Firmware-update label or status message for success, rendered on the Firmware page. */
#define TR_OTA_SUCCESS "Firmware written and verified successfully."
/** Firmware-update label or status message for rebooting, rendered on the Firmware page. */
#define TR_OTA_REBOOTING "Rebooting into the new firmware now..."

/** @} */

/**
 * @name Common field/fieldset labels (auto-extracted from pages/<page>.c source files)
 * @{
 */
/** Form label for the "add timestamp" field or fieldset, rendered on the configuration forms. */
#define TR_F_ADD_TIMESTAMP "Add timestamp"
/** Form label for the "altitude m" field or fieldset, rendered on the configuration forms. */
#define TR_F_ALTITUDE_M "Altitude (m)"
/** Form label for the "aprs is server" field or fieldset, rendered on the configuration forms. */
#define TR_F_APRS_IS_SERVER "APRS-IS Server"
/** Form label for the "aprs messaging" field or fieldset, rendered on the configuration forms. */
#define TR_F_APRS_MESSAGING "APRS Messaging"
/** Form label for the "aprs passcode" field or fieldset, rendered on the configuration forms. */
#define TR_F_APRS_PASSCODE "APRS-IS Passcode"
/** Form label for the "aprs symbols" field or fieldset, rendered on the configuration forms. */
#define TR_F_APRS_SYMBOLS "APRS Symbols"
/** Form label for the "audio afsk" field or fieldset, rendered on the configuration forms. */
#define TR_F_AUDIO_AFSK "Audio / AFSK"
/** Form label for the "enable audio modem" field or fieldset, rendered on the configuration forms. */
#define TR_F_ENABLE_AUDIO_MODEM "Enable audio ADC/DAC modem"
/** Form label for the "afsk modulation" field or fieldset, rendered on the configuration forms. */
#define TR_F_AFSK_MODULATION "Modulation"
/** Form label for the "audio low pass filter" field or fieldset, rendered on the configuration forms. */
#define TR_F_AUDIO_LOW_PASS_FILTER "Audio low-pass filter"
/** Form label for the "beacon interval s" field or fieldset, rendered on the configuration forms. */
#define TR_F_BEACON_INTERVAL_S "Beacon interval (s)"
/** Form label for the "beacon position" field or fieldset, rendered on the configuration forms. */
#define TR_F_BEACON_POSITION "Beacon / Position"
/** Form label for the "beacon position 2" field or fieldset, rendered on the configuration forms. */
#define TR_F_BEACON_POSITION_2 "Beacon position"
/** Form label for the "beacon via internet" field or fieldset, rendered on the configuration forms. */
#define TR_F_BEACON_VIA_INTERNET "Beacon via Internet"
/** Form label for the "beacon via rf" field or fieldset, rendered on the configuration forms. */
#define TR_F_BEACON_VIA_RF "Beacon via RF"
/** Form label for the "comment" field or fieldset, rendered on the configuration forms. */
#define TR_F_COMMENT "Comment"
/** Form label for the "compress position" field or fieldset, rendered on the configuration forms. */
#define TR_F_COMPRESS_POSITION "Compress position"
/** Form label for the "dashboard" field or fieldset, rendered on the configuration forms. */
#define TR_F_DASHBOARD "Dashboard"
/** Form label for the "data interval s" field or fieldset, rendered on the configuration forms. */
#define TR_F_DATA_INTERVAL_S "Data interval (s)"
/** Form label for the "digipeater" field or fieldset, rendered on the configuration forms. */
#define TR_F_DIGIPEATER "Digipeater"
/** Form label for the "enable" field or fieldset, rendered on the configuration forms. */
#define TR_F_ENABLE "Enable"
/** Form label for the "enable digipeater" field or fieldset, rendered on the configuration forms. */
#define TR_F_ENABLE_DIGIPEATER "Enable Digipeater"
/** Form label for the "enable igate" field or fieldset, rendered on the configuration forms. */
#define TR_F_ENABLE_IGATE "Enable IGate"
/** Form label for the "enable messaging" field or fieldset, rendered on the configuration forms. */
#define TR_F_ENABLE_MESSAGING "Enable messaging"
/** Form label for the "enable tracker" field or fieldset, rendered on the configuration forms. */
#define TR_F_ENABLE_TRACKER "Enable Tracker"
/** Form label for the "enable wx" field or fieldset, rendered on the configuration forms. */
#define TR_F_ENABLE_WX "Enable WX"
/** Form label for the "file storage" field or fieldset, rendered on the configuration forms. */
#define TR_F_FILE_STORAGE "File Storage"
/** Form label for the "filter" field or fieldset, rendered on the configuration forms. */
#define TR_F_FILTER "Filter"
/** Form label for the "fixed altitude m" field or fieldset, rendered on the configuration forms. */
#define TR_F_FIXED_ALTITUDE_M "Fixed Altitude (m)"
/** Form label for the "fixed interval s" field or fieldset, rendered on the configuration forms. */
#define TR_F_FIXED_INTERVAL_S "Fixed interval (s)"
/** Form label for the "fixed latitude" field or fieldset, rendered on the configuration forms. */
#define TR_F_FIXED_LATITUDE "Fixed Latitude"
/** Form label for the "fixed longitude" field or fieldset, rendered on the configuration forms. */
#define TR_F_FIXED_LONGITUDE "Fixed Longitude"
/** Form label for the "fx 25 forward error corrected ax 25" field or fieldset, rendered on the configuration forms. */
#define TR_F_FX_25_FORWARD_ERROR_CORRECTED_AX_25 "FX.25 (forward-error-corrected AX.25)"
/** Form label for the "igate" field or fieldset, rendered on the configuration forms. */
#define TR_F_IGATE "IGate"
/** Form label for the "include altitude" field or fieldset, rendered on the configuration forms. */
#define TR_F_INCLUDE_ALTITUDE "Include altitude"
/** Form label for the "tracker phg" field or fieldset, rendered on the Tracker page. */
#define TR_F_TRACKER_PHG "Include PHG data extension"
/** Form label for the "internet to rf" field or fieldset, rendered on the configuration forms. */
#define TR_F_INTERNET_TO_RF "Internet to RF"
/** Form label for the "latitude" field or fieldset, rendered on the configuration forms. */
#define TR_F_LATITUDE "Latitude"
/** Form label for the "longitude" field or fieldset, rendered on the configuration forms. */
#define TR_F_LONGITUDE "Longitude"
/** Form label for the "message" field or fieldset, rendered on the configuration forms. */
#define TR_F_MESSAGE "Message"
/** Form label for the "message alarm enable" field or fieldset, rendered on the configuration forms. */
#define TR_F_MESSAGE_ALARM_ENABLE "Enable Message Alarm"
/** Form label for the "message alarm pin" field or fieldset, rendered on the configuration forms. */
#define TR_F_MESSAGE_ALARM_PIN "Message Alarm pin"
/** Fieldset label for the operator-defined message-group name slots on the Message page, rendered on the configuration forms. */
#define TR_F_MESSAGE_GROUPS "Message Groups"
/** Form label for one operator-defined message-group name slot, "%d" is the slot number (1-based), rendered on the Message page. */
#define TR_F_MESSAGE_GROUP_FMT "Group %d"
/** Option label for the Mic-E position comment M0 (Off Duty), rendered on the Tracker page. */
#define TR_F_MICE_MSG_M0 "M0 Off Duty"
/** Option label for the Mic-E position comment M1 (En Route), rendered on the Tracker page. */
#define TR_F_MICE_MSG_M1 "M1 En Route"
/** Option label for the Mic-E position comment M2 (In Service), rendered on the Tracker page. */
#define TR_F_MICE_MSG_M2 "M2 In Service"
/** Option label for the Mic-E position comment M3 (Returning), rendered on the Tracker page. */
#define TR_F_MICE_MSG_M3 "M3 Returning"
/** Option label for the Mic-E position comment M4 (Committed), rendered on the Tracker page. */
#define TR_F_MICE_MSG_M4 "M4 Committed"
/** Option label for the Mic-E position comment M5 (Special), rendered on the Tracker page. */
#define TR_F_MICE_MSG_M5 "M5 Special"
/** Option label for the Mic-E position comment M6 (Priority), rendered on the Tracker page. */
#define TR_F_MICE_MSG_M6 "M6 Priority"
/** Word prefixed to the number of each locally defined Mic-E position comment C0-C6, rendered on the Tracker page. */
#define TR_F_MICE_MSG_CUSTOM "Custom"
/** Form label for the "mice position" field or fieldset, rendered on the configuration forms. */
#define TR_F_MICE_POSITION "Mic-E position encoding"
/** Form label for the "mice position comment" field or fieldset, rendered on the configuration forms. */
#define TR_F_MICE_POSITION_COMMENT "Mic-E position comment"
/** Form label for the "mode" field or fieldset, rendered on the configuration forms. */
#define TR_F_MODE "Mode"
/** Form label for the "my callsign" field or fieldset, rendered on the configuration forms. */
#define TR_F_MY_CALLSIGN "My Callsign"
/** Caption of the control that copies the station identity into the current form. */
#define TR_USE_MY_STATION_DATA "Use My Station Data"
/** Caption of the control that fills the current form's position/motion fields live from the GNSS receiver. */
#define TR_USE_GPS_DATA "Use GPS"
/** Form label for the Tracker page checkbox that has the tracker beacon read the GNSS receiver at every transmission instead of beaconing the fixed
    position above. */
#define TR_F_TRACKER_USE_LIVE_GPS "Use live GPS fix"
/** Form label for the "name" field or fieldset, rendered on the configuration forms. */
#define TR_F_NAME "Name"
/** Form label for the "object item name" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJECT_ITEM_NAME "Object/Item name"
/** Form label for the "object name" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJECT_NAME "Object name"
/** Form label for the "options" field or fieldset, rendered on the configuration forms. */
#define TR_F_OPTIONS "Options"
/** Form label for the "parm unit eqns interval s" field or fieldset, rendered on the configuration forms. */
#define TR_F_PARM_UNIT_EQNS_INTERVAL_S "PARM/UNIT/EQNS interval (s)"
/** Form label for the "password" field or fieldset, rendered on the configuration forms. */
#define TR_F_PASSWORD "Password"
/** Form label for the "position" field or fieldset, rendered on the configuration forms. */
#define TR_F_POSITION "Position"
/** Form label for the "preamble ms" field or fieldset, rendered on the configuration forms. */
#define TR_F_PREAMBLE_MS "Preamble (ms)"
/** Selector entry meaning the feature or pin is switched off. */
#define TR_DISABLED "Disabled"
/** Format string for a GPIO selector entry that is already claimed, taking the pin number and the claiming peripheral. */
#define TR_GPIO_USED_BY "GPIO%d (used: %.30s)"
/** Form label for the "protocol" field or fieldset, rendered on the configuration forms. */
#define TR_F_PROTOCOL "Protocol"
/** Form label for the "query" field or fieldset, rendered on the configuration forms. */
#define TR_F_QUERY "Query"
/** Form label for the "enable query" field or fieldset, rendered on the configuration forms. */
#define TR_F_ENABLE_QUERY "Enable query responder"
/** Form label for the "query rf" field or fieldset, rendered on the configuration forms. */
#define TR_F_QUERY_RF "Answer queries heard on RF"
/** Form label for the "query inet" field or fieldset, rendered on the configuration forms. */
#define TR_F_QUERY_INET "Answer queries heard from APRS-IS"
/** Form label for the "query aprs" field or fieldset, rendered on the configuration forms. */
#define TR_F_QUERY_APRS "?APRS? - general station query"
/** Form label for the "query wx" field or fieldset, rendered on the configuration forms. */
#define TR_F_QUERY_WX "?WX? - weather report request"
/** Form label for the "query igate" field or fieldset, rendered on the configuration forms. */
#define TR_F_QUERY_IGATE "?IGATE? - IGate status request"
/** Form label for the "query directed" field or fieldset, rendered on the configuration forms. */
#define TR_F_QUERY_DIRECTED "Directed queries (CALL:?query?)"
/** Form label for the "query ext" field or fieldset, rendered on the configuration forms. */
#define TR_F_QUERY_EXT "Extended directed queries (?APRSD/?APRSH/?APRSM/?APRSO/?APRSP/?APRSS/?APRST)"
/** Form label for the "query min interval" field or fieldset, rendered on the configuration forms. */
#define TR_F_QUERY_MIN_INTERVAL "Minimum seconds between identical responses"
/** Form label for the "query cap section" field or fieldset, rendered on the configuration forms. */
#define TR_F_QUERY_CAP_SECTION "Station capabilities beacon"
/** Form label for the "query cap enable" field or fieldset, rendered on the configuration forms. */
#define TR_F_QUERY_CAP_ENABLE "Send capabilities periodically"
/** Form label for the "query cap interval" field or fieldset, rendered on the configuration forms. */
#define TR_F_QUERY_CAP_INTERVAL "Capabilities beacon interval (s)"
/** Form label for the "query cap extra" field or fieldset, rendered on the configuration forms. */
#define TR_F_QUERY_CAP_EXTRA "Additional capability tokens"
/** Form label for the "radio modem" field or fieldset, rendered on the configuration forms. */
#define TR_F_RADIO_MODEM "Radiomodem"
/** Form label for the "retry count" field or fieldset, rendered on the configuration forms. */
#define TR_F_RETRY_COUNT "Retry count"
/** Form label for the "retry interval s" field or fieldset, rendered on the configuration forms. */
#define TR_F_RETRY_INTERVAL_S "Retry interval (s)"
/** Form label for the "rf to internet" field or fieldset, rendered on the configuration forms. */
#define TR_F_RF_TO_INTERNET "RF to Internet"
/** Form label for the "rf tx buffers" field or fieldset, rendered on the configuration forms. */
#define TR_F_RF_TX_BUFFERS "TX buffers"
/** Form label for the "duty cycle en" field or fieldset, rendered on the configuration forms. */
#define TR_F_DUTY_CYCLE_EN "Duty-cycle limiter"
/** Form label for the "duty cycle pct" field or fieldset, rendered on the configuration forms. */
#define TR_F_DUTY_CYCLE_PCT "Duty-cycle limit (%)"
/** Form label for the "ptt min unkey ms" field or fieldset, rendered on the configuration forms. */
#define TR_F_PTT_MIN_UNKEY_MS "PTT minimum unkey time (ms)"
/** Form label for the "csma persistence" field or fieldset, rendered on the configuration forms. */
#define TR_F_CSMA_PERSISTENCE "CSMA persistence (p, 1-255)"
/** Form label for the "send receive via internet" field or fieldset, rendered on the configuration forms. */
#define TR_F_SEND_RECEIVE_VIA_INTERNET "Send/receive via Internet"
/** Form label for the "send receive via rf" field or fieldset, rendered on the configuration forms. */
#define TR_F_SEND_RECEIVE_VIA_RF "Send/receive via RF"
/** Form label for the "send via internet" field or fieldset, rendered on the configuration forms. */
#define TR_F_SEND_VIA_INTERNET "Send via Internet"
/** Form label for the "send via rf" field or fieldset, rendered on the configuration forms. */
#define TR_F_SEND_VIA_RF "Send via RF"
/** Form label for the "sensor mapping enable averaged source channel" field or fieldset, rendered on the configuration forms. */
#define TR_F_SENSOR_MAPPING_ENABLE_AVERAGED_SOURCE_CHANNEL "Sensor Mapping (enable / averaged / source channel)"
/** Form label for the "server host" field or fieldset, rendered on the configuration forms. */
#define TR_F_SERVER_HOST "Server Host"
/** Form label for the "server port" field or fieldset, rendered on the configuration forms. */
#define TR_F_SERVER_PORT "Server Port"
/** Fieldset label for the Tracker page's SmartBeaconing settings, rendered on the configuration forms. */
#define TR_F_SMARTBEACONING "SmartBeaconing"
/** Form label for the "enable smartbeaconing" checkbox, rendered on the Tracker page. */
#define TR_F_SMARTBEACONING_ENABLE "Enable SmartBeaconing"
/** Form label for the SmartBeaconing slow-rate (stationary) beacon interval, rendered on the Tracker page. */
#define TR_F_SMARTBEACONING_SLOW_INTERVAL_S "Slow-rate interval (s)"
/** Form label for the SmartBeaconing fast-rate (moving) beacon interval, rendered on the Tracker page. */
#define TR_F_SMARTBEACONING_FAST_INTERVAL_S "Fast-rate interval (s)"
/** Form label for the SmartBeaconing low-speed threshold, rendered on the Tracker page. */
#define TR_F_SMARTBEACONING_LOW_SPEED_KMH "Low speed (km/h)"
/** Form label for the SmartBeaconing high-speed threshold, rendered on the Tracker page. */
#define TR_F_SMARTBEACONING_HIGH_SPEED_KMH "High speed (km/h)"
/** Form label for the SmartBeaconing corner-pegging minimum turn angle, rendered on the Tracker page. */
#define TR_F_SMARTBEACONING_TURN_ANGLE "Turn angle (deg)"
/** Form label for the SmartBeaconing corner-pegging turn slope, rendered on the Tracker page. */
#define TR_F_SMARTBEACONING_TURN_SLOPE "Turn slope (deg)"
/** Form label for the SmartBeaconing corner-pegging minimum turn time, rendered on the Tracker page. */
#define TR_F_SMARTBEACONING_MIN_TURN_TIME_S "Minimum turn time (s)"
/** Form label for the "ssid" field or fieldset, rendered on the configuration forms. */
#define TR_F_SSID "SSID"
/** Form label for the "station" field or fieldset, rendered on the configuration forms. */
#define TR_F_STATION "Station"
/** Form label for the "bulletins" field or fieldset, rendered on the configuration forms. */
#define TR_F_BULLETINS "Bulletins"
/** Form label for the "bulletin fmt" field or fieldset, rendered on the configuration forms. */
#define TR_F_BULLETIN_FMT "Bulletin %d"
/** Form label for the "bulletin id" field or fieldset, rendered on the configuration forms. */
#define TR_F_BULLETIN_ID "Identifier (0-9 bulletin, A-Z announcement)"
/** Form label for the "bulletin group" field or fieldset, rendered on the configuration forms. */
#define TR_F_BULLETIN_GROUP "Group (up to 5 chars, empty = general)"
/** Form label for the "bulletin msg" field or fieldset, rendered on the configuration forms. */
#define TR_F_BULLETIN_MSG "Message (max 67 chars)"
/** Form label for the "bulletin expire" field or fieldset, rendered on the configuration forms. */
#define TR_F_BULLETIN_EXPIRE "Expire (hours, 0 = never)"
/** Form label for the "objitems" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEMS "Objects and Items"
/** Form label for the "objitem fmt" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_FMT "Object/Item %d"
/** Form label for the "objitem tx control" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_TX_CONTROL "Transmission Control"
/** Form label for the "objitem identity" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_IDENTITY "Identity & State"
/** Form label for the "objitem pos symbol" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_POS_SYMBOL "Position & Symbol"
/** Form label for the "objitem area section" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_AREA_SECTION "Area Object"
/** Form label for the "objitem signpost section" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_SIGNPOST_SECTION "Signpost"
/** Form label for the "objitem repeater section" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_REPEATER_SECTION "Repeater Radio Parameters"
/** Form label for the "objitem timing section" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_TIMING_SECTION "Beacon Timing"
/** Form label for the "objitem type" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_TYPE "Type"
/** Form label for the "objitem type object" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_TYPE_OBJECT "Object (timestamped)"
/** Form label for the "objitem type item" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_TYPE_ITEM "Item (non-timestamped)"
/** Form label for the "objitem permanent" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_PERMANENT "Permanent (Object only, 111111z)"
/** Form label for the "objitem permanent note" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_PERMANENT_NOTE                                                                                                                            \
    "A permanent Object is sent with the fixed 111111z timestamp instead of the live time, so it is never replaced by anyone else's similarly named "          \
    "Object - only the originating station may update or move it. Has no effect on an Item."
/** Form label for the "objitem active" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_ACTIVE "Active (uncheck = kill)"
/** Form label for the "objitem scope" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_SCOPE "Scope"
/** Form label for the "objitem scope private" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_SCOPE_PRIVATE "Private (not transmitted)"
/** Form label for the "objitem scope local" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_SCOPE_LOCAL "Local (RF only)"
/** Form label for the "objitem scope global" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_SCOPE_GLOBAL "Global (RF + Internet)"
/** Form label for the "objitem symbol" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_SYMBOL "Symbol / overlay"
/** Form label for the "objitem course" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_COURSE "Course (deg, 0-359)"
/** Form label for the "objitem speed" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_SPEED "Speed (knots, 0 = omit)"
/** Form label for the "objitem area shape" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_AREA_SHAPE "Area shape (\\l symbol)"
/** Form label for the "objitem shape circle" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_SHAPE_CIRCLE "Circle"
/** Form label for the "objitem shape line down right" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_SHAPE_LINE_DOWN_RIGHT "Line (down/right)"
/** Form label for the "objitem shape ellipse" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_SHAPE_ELLIPSE "Ellipse"
/** Form label for the "objitem shape triangle" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_SHAPE_TRIANGLE "Triangle"
/** Form label for the "objitem shape box" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_SHAPE_BOX "Box"
/** Form label for the "objitem shape filled" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_SHAPE_FILLED " (filled)"
/** Form label for the "objitem area color" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_AREA_COLOR "Area color (0-15)"
/** Form label for the "objitem area lat off" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_AREA_LAT_OFF "Area latitude offset (deg)"
/** Form label for the "objitem area lon off" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_AREA_LON_OFF "Area longitude offset (deg)"
/** Form label for the "objitem shape line down left" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_SHAPE_LINE_DOWN_LEFT "Line (down/left)"
/** Form label for the "objitem area width" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_AREA_WIDTH "Line corridor width (miles, 0 = omit)"
/** Form label for the "objitem signpost" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_SIGNPOST "Signpost text (\\m symbol, 3 chars)"
/** Form label for the "objitem freq" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_FREQ "Monitor frequency (MHz, 0 = none)"
/** Form label for the "objitem rx freq enable" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_RX_FREQ_ENABLE "Independent receive frequency"
/** Form label for the "objitem rx freq" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_RX_FREQ "Receive frequency (MHz, split TX/RX)"
/** Form label for the "objitem duplex" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_DUPLEX "Duplex direction"
/** Form label for the "objitem duplex simplex" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_DUPLEX_SIMPLEX "Simplex"
/** Form label for the "objitem duplex plus" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_DUPLEX_PLUS "Plus (+)"
/** Form label for the "objitem duplex minus" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_DUPLEX_MINUS "Minus (-)"
/** Form label for the "objitem offset" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_OFFSET "Duplex offset (kHz)"
/** Form label for the "objitem dcs enable" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_DCS_ENABLE "Use DCS code instead of CTCSS tone"
/** Form label for the "objitem tone" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_TONE "Subaudible tone CTCSS (Hz, 0 = none)"
/** Form label for the "objitem dcs code" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_DCS_CODE "DCS code (octal, 0-511)"
/** Form label for the "objitem narrow" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_NARROW "Narrowband modulation"
/** Form label for the "objitem range" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_RANGE "Coverage range (0 = none)"
/** Form label for the "objitem range unit" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_RANGE_UNIT "Range unit"
/** Form label for the "objitem range unit mi" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_RANGE_UNIT_MI "Miles"
/** Form label for the "objitem range unit km" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_RANGE_UNIT_KM "Kilometers"
/** Form label for the "objitem path fmt" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_PATH_FMT "Path %d"
/** Form label for the "objitem qru" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_QRU "QRU group membership"
/** Form label for the "objitem qru none" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_QRU_NONE "(none)"
/** Form label for the "qru ambu" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_AMBU "ambulance"
/** Form label for the "qru club" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_CLUB "ham radio club"
/** Form label for the "qru echo" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_ECHO "Echolink"
/** Form label for the "qru fire" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_FIRE "fire station"
/** Form label for the "qru food" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_FOOD "restaurants"
/** Form label for the "qru fuel" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_FUEL "gas/petrol stations"
/** Form label for the "qru hosp" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_HOSP "hospitals"
/** Form label for the "qru lifeboat" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_LIFEBOAT "lifeboats"
/** Form label for the "qru lths" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_LTHS "lighthouses"
/** Form label for the "qru poli" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_POLI "police stations"
/** Form label for the "qru post" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_POST "post offices"
/** Form label for the "qru rd13" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_RD13 "D-Star 13cm repeaters"
/** Form label for the "qru rd23" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_RD23 "D-Star 23cm repeaters"
/** Form label for the "qru rd2m" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_RD2M "D-Star 2m repeaters"
/** Form label for the "qru rd3c" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_RD3C "D-Star 3cm repeaters"
/** Form label for the "qru rd70" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_RD70 "D-Star 70cm repeaters"
/** Form label for the "qru rp10" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_RP10 "analog 10m repeaters"
/** Form label for the "qru rp13" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_RP13 "analog 13cm repeaters"
/** Form label for the "qru rp23" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_RP23 "analog 23cm repeaters"
/** Form label for the "qru rp2m" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_RP2M "analog 2m repeaters"
/** Form label for the "qru rp3c" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_RP3C "analog 3cm repeaters"
/** Form label for the "qru rp6m" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_RP6M "analog 6m repeaters"
/** Form label for the "qru rp70" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_RP70 "analog 70cm repeaters"
/** Form label for the "qru rt13" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_RT13 "television 13cm repeaters"
/** Form label for the "qru rt23" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_RT23 "television 23cm repeaters"
/** Form label for the "qru rt3c" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_RT3C "television 3cm repeaters"
/** Form label for the "qru srail" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_SRAIL "steam railroad"
/** Form label for the "qru stor" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_STOR "Amateur Radio stores"
/** Form label for the "qru t2srv" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_T2SRV "approx. locations of Tier 2 APRS-IS servers"
/** Form label for the "qru vete" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_VETE "veterinarians"
/** Form label for the "qru wota" field or fieldset, rendered on the configuration forms. */
#define TR_F_QRU_WOTA "Wainwrights On The Air"
/** Form label for the "objitem init rate" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_INIT_RATE "Initial repeat rate (s)"
/** Form label for the "objitem slow rate" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_SLOW_RATE "Slow repeat rate (s, 0 = no decay)"
/** Form label for the "objitem decay" field or fieldset, rendered on the configuration forms. */
#define TR_F_OBJITEM_DECAY "Decay ratio (e.g. 2.0, <1 = none)"
/** Explanatory note shown beside the objitem setting, rendered on the configuration forms. */
#define TR_NOTE_OBJITEM                                                                                                                                        \
    "Objects are timestamped (;NAME); Items are never timestamped ()NAME). Unchecking Active sends kill reports, then auto-disables. Scope limits "            \
    "transmission regardless of the RF/Internet checks."
/** Form label for the "status beacon" field or fieldset, rendered on the configuration forms. */
#define TR_F_STATUS_BEACON "Status Beacon"
/** Form label for the "status interval s 0 off" field or fieldset, rendered on the configuration forms. */
#define TR_F_STATUS_INTERVAL_S_0_OFF "Status interval (s, 0=off)"
/** Form label for the "status text" field or fieldset, rendered on the configuration forms. */
#define TR_F_STATUS_TEXT "Status text"
/** Form label for the "system" field or fieldset, rendered on the configuration forms. */
#define TR_F_SYSTEM "System"
/** Form label for the "telemetry" field or fieldset, rendered on the configuration forms. */
#define TR_F_TELEMETRY "Telemetry"
/** Form label for the "beacon" field or fieldset, rendered on the configuration forms. */
#define TR_F_BEACON "Beacon"
/** Form label for the "tracker" field or fieldset, rendered on the configuration forms. */
#define TR_F_TRACKER "Tracker"
/** Form label for the "tx time slot ms" field or fieldset, rendered on the configuration forms. */
#define TR_F_TX_TIME_SLOT_MS "TX time-slot (ms)"
/** Form label for the "upload" field or fieldset, rendered on the configuration forms. */
#define TR_F_UPLOAD "Upload"
/** Form label for the "username" field or fieldset, rendered on the configuration forms. */
#define TR_F_USERNAME "Username"
/** Form label for the "weather" field or fieldset, rendered on the configuration forms. */
#define TR_F_WEATHER "Weather"
/** Form label for the "weather station" field or fieldset, rendered on the configuration forms. */
#define TR_F_WEATHER_STATION "Weather Station"
/** Form label for the "wireless" field or fieldset, rendered on the configuration forms. */
#define TR_F_WIRELESS "Wireless"

/** Form label for the "off" field or fieldset, rendered on the configuration forms. */
#define TR_F_OFF "Off"

/** @} */

/**
 * @name page_common.c: dashboard / sysinfo
 * @{
 */
/** Interface string labelling enabled, rendered on the dashboard. */
#define TR_ENABLED "enabled"
/** Dashboard label or value for digi short, rendered on the dashboard. */
#define TR_DASH_DIGI_SHORT "Digi"
/** Dashboard label or value for wx short, rendered on the dashboard. */
#define TR_DASH_WX_SHORT "WX:"
/** Dashboard label or value for datetime, rendered on the dashboard. */
#define TR_DASH_DATETIME "Date/Time:"
/** Dashboard label or value for uptime, rendered on the dashboard. */
#define TR_DASH_UPTIME "Uptime:"
/** Dashboard label or value for free heap, rendered on the dashboard. */
#define TR_DASH_FREE_HEAP "Free heap:"
/** Dashboard label or value for littlefs, rendered on the dashboard. */
#define TR_DASH_LITTLEFS "LittleFS:"
/** Dashboard label or value for sysinfo, rendered on the dashboard. */
#define TR_DASH_SYSINFO "System Info"
/** Dashboard label or value for igate traffic, rendered on the dashboard. */
#define TR_DASH_IGATE_TRAFFIC "IGate Traffic"
/** Traffic-log column heading or label for pause, rendered on the dashboard. */
#define TR_TRAFFIC_PAUSE "Pause"
/** Traffic-log column heading or label for resume, rendered on the dashboard. */
#define TR_TRAFFIC_RESUME "Resume"
/** Traffic-log column heading or label for clear, rendered on the dashboard. */
#define TR_TRAFFIC_CLEAR "Clear"
/** Traffic-log column heading or label for waiting, rendered on the dashboard. */
#define TR_TRAFFIC_WAITING "Waiting for traffic..."
/** Traffic-log column heading or label for col time, rendered on the dashboard. */
#define TR_TRAFFIC_COL_TIME "TIME"
/** Traffic-log column heading or label for col type, rendered on the dashboard. */
#define TR_TRAFFIC_COL_TYPE "TYPE"
/** Traffic-log column heading or label for col dx, rendered on the dashboard. */
#define TR_TRAFFIC_COL_DX "DX"
/** Traffic-log column heading or label for col packet, rendered on the dashboard. */
#define TR_TRAFFIC_COL_PACKET "PACKET"
/** Traffic-log column heading or label for col audio, rendered on the dashboard. */
#define TR_TRAFFIC_COL_AUDIO "AUDIO"
/** Column header of the IGate traffic table holding the fields decoded out of the packet. */
#define TR_TRAFFIC_COL_DECODED "DECODED"
/** System-information row label for chip, rendered on the dashboard. */
#define TR_SYSINFO_CHIP "Chip"
/** System-information row label for model, rendered on the dashboard. */
#define TR_SYSINFO_MODEL "Model:"
/** System-information row label for cores, rendered on the dashboard. */
#define TR_SYSINFO_CORES "Cores:"
/** System-information row label for revision, rendered on the dashboard. */
#define TR_SYSINFO_REVISION "Revision:"
/** System-information row label for cpu freq, rendered on the dashboard. */
#define TR_SYSINFO_CPU_FREQ "CPU speed:"
/** System-information row label for cpu freq set, rendered on the dashboard. */
#define TR_SYSINFO_CPU_FREQ_SET "Set CPU frequency"
/** System-information row label for cpu freq note, rendered on the dashboard. */
#define TR_SYSINFO_CPU_FREQ_NOTE "Saved to flash and re-applied automatically on every boot."
/** System-information row label for flash size, rendered on the dashboard. */
#define TR_SYSINFO_FLASH_SIZE "Flash size:"
/** System-information row label for min free heap, rendered on the dashboard. */
#define TR_SYSINFO_MIN_FREE_HEAP "Min free heap:"
/** Dashboard label or value for reboot reason, rendered on the dashboard. */
#define TR_DASH_REBOOT_REASON "Reboot Reason:"

/** @} */

/**
 * @name page_common.c
 * @{
 */
/** Dashboard label or value for radio info, rendered on the dashboard. */
#define TR_DASH_RADIO_INFO "Radio Info"
/** Dashboard label or value for modem, rendered on the dashboard. */
#define TR_DASH_MODEM "MODEM"
/** Dashboard label or value for fx25, rendered on the dashboard. */
#define TR_DASH_FX25 "FX.25"
/** Dashboard label or value for aprs is server, rendered on the dashboard. */
#define TR_DASH_APRS_IS_SERVER "APRS-IS SERVER"
/** Dashboard label or value for host, rendered on the dashboard. */
#define TR_DASH_HOST "HOST"
/** Dashboard label or value for port, rendered on the dashboard. */
#define TR_DASH_PORT "PORT"
/** Dashboard label or value for wifi, rendered on the dashboard. */
#define TR_DASH_WIFI "WiFi"
/** Dashboard label or value for mode, rendered on the dashboard. */
#define TR_DASH_MODE "MODE"
/** Dashboard label or value for ssid, rendered on the dashboard. */
#define TR_DASH_SSID "SSID"
/** Dashboard label or value for rssi, rendered on the dashboard. */
#define TR_DASH_RSSI "RSSI"
/** Dashboard label or value for disconnected, rendered on the dashboard. */
#define TR_DASH_DISCONNECTED "Disconnect"
/** Dashboard label or value for modes enabled, rendered on the dashboard. */
#define TR_DASH_MODES_ENABLED "Modes Enabled"
/** Dashboard label or value for network status, rendered on the dashboard. */
#define TR_DASH_NETWORK_STATUS "Network Status"
/** Dashboard label or value for statistics, rendered on the dashboard. */
#define TR_DASH_STATISTICS "STATISTICS"
/** Dashboard label or value for radio rx, rendered on the dashboard. */
#define TR_DASH_RADIO_RX "RADIO RX:"
/** Dashboard label or value for packet tx, rendered on the dashboard. */
#define TR_DASH_PACKET_TX "RADIO TX:"
/** Dashboard label or value for rf2inet, rendered on the dashboard. */
#define TR_DASH_RF2INET "RF2INET:"
/** Dashboard label or value for inet2rf, rendered on the dashboard. */
#define TR_DASH_INET2RF "INET2RF:"
/** Dashboard label or value for igate rx, rendered on the dashboard. */
#define TR_DASH_IGATE_RX "IGATE RX:"
/** Dashboard label or value for igate tx, rendered on the dashboard. */
#define TR_DASH_IGATE_TX "IGATE TX:"
/** Dashboard label or value for digi stat, rendered on the dashboard. */
#define TR_DASH_DIGI_STAT "DIGI:"
/** Dashboard label or value for drop err, rendered on the dashboard. */
#define TR_DASH_DROP_ERR "DROP/ERR:"
/** Dashboard label or value for drop breakdown, rendered on the dashboard. */
#define TR_DASH_DROP_BREAKDOWN "Drop Breakdown"
/** Dashboard label or value for tx queue, rendered on the dashboard. */
#define TR_DASH_TX_QUEUE "RF TX QUEUE:"
/** Dashboard label or value for csma forced, rendered on the dashboard. */
#define TR_DASH_CSMA_FORCED "CSMA FORCED (BUSY/PERSIST):"
/** Dashboard label or value for tx duty cycle, rendered on the dashboard. */
#define TR_DASH_TX_DUTY_CYCLE "TX DUTY CYCLE:"
/** Dashboard label or value for lh icon, rendered on the dashboard. */
#define TR_DASH_LH_ICON "ICON"

/** @} */

/**
 * @name page_digi.c / page_igate.c / page_tracker.c telemetry notes
 * @{
 */
/** Explanatory note shown beside the tlm digi setting, rendered on the Digipeater, IGate and Tracker pages. */
#define TR_NOTE_TLM_DIGI                                                                                                                                       \
    "Telemetry (EQNS/PARM/UNIT) for Digi beacons is configured on the "                                                                                        \
    "<a href='/tlm'>Telemetry</a> page."
/** Explanatory note shown beside the tlm igate setting, rendered on the Digipeater, IGate and Tracker pages. */
#define TR_NOTE_TLM_IGATE                                                                                                                                      \
    "Telemetry (EQNS/PARM/UNIT) for IGate beacons is configured on the "                                                                                       \
    "<a href='/tlm'>Telemetry</a> page."
/** Explanatory note shown beside the tlm tracker setting, rendered on the Digipeater, IGate and Tracker pages. */
#define TR_NOTE_TLM_TRACKER                                                                                                                                    \
    "Telemetry (EQNS/PARM/UNIT) for Tracker beacons is configured on the "                                                                                     \
    "<a href='/tlm'>Telemetry</a> page."

/** @} */

/**
 * @name page_msgchat.c ("Snd/Rcv Msg")
 * @{
 */
/** Form label for the "snd rcv msg" field or fieldset, rendered on the Send/Receive Message page. */
#define TR_F_SND_RCV_MSG "Snd/Rcv Msg"
/** Message page label for my station, rendered on the Send/Receive Message page. */
#define TR_MSGCHAT_MY_STATION "My Station:"
/** Message page label for disabled note, rendered on the Send/Receive Message page. */
#define TR_MSGCHAT_DISABLED_NOTE "APRS Messaging is disabled or no station callsign is configured. Enable it and set a callsign on the Message page first."
/** Message page label for loading, rendered on the Send/Receive Message page. */
#define TR_MSGCHAT_LOADING "Loading messages..."
/** Message page label for empty, rendered on the Send/Receive Message page. */
#define TR_MSGCHAT_EMPTY "No messages yet."
/** Message page label for to, rendered on the Send/Receive Message page. */
#define TR_MSGCHAT_TO "To (callsign):"
/** Message page label for to placeholder, rendered on the Send/Receive Message page. */
#define TR_MSGCHAT_TO_PLACEHOLDER "N0CALL-9"
/** Message page label for text, rendered on the Send/Receive Message page. */
#define TR_MSGCHAT_TEXT "Message:"
/** Message page label for text placeholder, rendered on the Send/Receive Message page. */
#define TR_MSGCHAT_TEXT_PLACEHOLDER "Type a message..."
/** Message page label for send, rendered on the Send/Receive Message page. */
#define TR_MSGCHAT_SEND "Send"
/** Message page label for you, rendered on the Send/Receive Message page. */
#define TR_MSGCHAT_YOU "You"
/** Message page label for err empty, rendered on the Send/Receive Message page. */
#define TR_MSGCHAT_ERR_EMPTY "Enter a destination callsign and a message."
/** Message page label for err disabled, rendered on the Send/Receive Message page. */
#define TR_MSGCHAT_ERR_DISABLED "APRS Messaging is disabled on the Message page."
/** Message page label for err no mycall, rendered on the Send/Receive Message page. */
#define TR_MSGCHAT_ERR_NO_MYCALL "No station callsign configured."
/** Message page label for sent ok, rendered on the Send/Receive Message page. */
#define TR_MSGCHAT_SENT_OK "Sent."
/** Message page label for sent fail, rendered on the Send/Receive Message page. */
#define TR_MSGCHAT_SENT_FAIL "Send failed."

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
/** File Storage page label for usage, rendered on the File Storage page. */
#define TR_STORAGE_USAGE "LittleFS usage:"
/** File Storage page label for upload file, rendered on the File Storage page. */
#define TR_STORAGE_UPLOAD_FILE "Upload file"
/** File Storage page label for confirm format, rendered on the File Storage page. */
#define TR_STORAGE_CONFIRM_FORMAT "Erase ALL files and reset config to defaults?"
/** File Storage page label for format btn, rendered on the File Storage page. */
#define TR_STORAGE_FORMAT_BTN "Format LittleFS"
/** File Storage page label for size bytes, rendered on the File Storage page. */
#define TR_STORAGE_SIZE_BYTES "Size (bytes)"
/** File Storage page label for actions, rendered on the File Storage page. */
#define TR_STORAGE_ACTIONS "Actions"
/** File Storage page label for download, rendered on the File Storage page. */
#define TR_STORAGE_DOWNLOAD "Download"
/** File Storage page label for confirm delete prefix, rendered on the File Storage page. */
#define TR_STORAGE_CONFIRM_DELETE_PREFIX "Delete "
/** File Storage page label for delete, rendered on the File Storage page. */
#define TR_STORAGE_DELETE "Delete"
/** File Storage page label for upload ok, rendered on the File Storage page. */
#define TR_STORAGE_UPLOAD_OK "Uploaded:"
/** File Storage page label for upload failed, rendered on the File Storage page. */
#define TR_STORAGE_UPLOAD_FAILED "Upload failed. Check the file and make sure there is enough free space."
/** File Storage page label for no file chosen, rendered on the File Storage page. */
#define TR_STORAGE_NO_FILE_CHOSEN "Choose a file first."
/** File Storage page label for back, rendered on the File Storage page. */
#define TR_STORAGE_BACK "Back"

/** @} */

/**
 * @name page_symbol.c
 * @{
 */
/** Symbol-picker label for house hf, rendered on the symbol picker. */
#define TR_SYM_HOUSE_HF "House (HF)"
/** Symbol-picker label for car, rendered on the symbol picker. */
#define TR_SYM_CAR "Car"
/** Symbol-picker label for motorcycle, rendered on the symbol picker. */
#define TR_SYM_MOTORCYCLE "Motorcycle"
/** Symbol-picker label for bicycle, rendered on the symbol picker. */
#define TR_SYM_BICYCLE "Bicycle"
/** Symbol-picker label for truck, rendered on the symbol picker. */
#define TR_SYM_TRUCK "Truck"
/** Symbol-picker label for van, rendered on the symbol picker. */
#define TR_SYM_VAN "Van"
/** Symbol-picker label for jeep, rendered on the symbol picker. */
#define TR_SYM_JEEP "Jeep"
/** Symbol-picker label for fire truck, rendered on the symbol picker. */
#define TR_SYM_FIRE_TRUCK "Fire truck"
/** Symbol-picker label for police, rendered on the symbol picker. */
#define TR_SYM_POLICE "Police"
/** Symbol-picker label for house, rendered on the symbol picker. */
#define TR_SYM_HOUSE "House"
/** Symbol-picker label for digipeater, rendered on the symbol picker. */
#define TR_SYM_DIGIPEATER "Digipeater"
/** Symbol-picker label for gateway, rendered on the symbol picker. */
#define TR_SYM_GATEWAY "Gateway"
/** Symbol-picker label for weather station, rendered on the symbol picker. */
#define TR_SYM_WEATHER_STATION "Weather station"
/** Symbol-picker label for balloon, rendered on the symbol picker. */
#define TR_SYM_BALLOON "Balloon"
/** Symbol-picker label for space shuttle, rendered on the symbol picker. */
#define TR_SYM_SPACE_SHUTTLE "Space shuttle"
/** Symbol-picker label for sailboat, rendered on the symbol picker. */
#define TR_SYM_SAILBOAT "Sailboat"
/** Symbol-picker label for nws site, rendered on the symbol picker. */
#define TR_SYM_NWS_SITE "NWS site"
/** Symbol-picker label for tcp ip, rendered on the symbol picker. */
#define TR_SYM_TCP_IP "TCP/IP"
/** Symbol-picker label for car alt, rendered on the symbol picker. */
#define TR_SYM_CAR_ALT "Car (alternate table)"
/** Symbol-picker label for wx station alt, rendered on the symbol picker. */
#define TR_SYM_WX_STATION_ALT "WX station (alternate table)"
/** Symbol-picker label for intro, rendered on the symbol picker. */
#define TR_SYM_INTRO                                                                                                                                           \
    "Quick reference for common APRS symbol codes. Each service page "                                                                                         \
    "(IGate / Digi / Tracker) has its own free-text symbol field \u2014 copy the "                                                                             \
    "2-character code from here into that field."
/** Symbol-picker label for code, rendered on the symbol picker. */
#define TR_SYM_CODE "Code"
/** Symbol-picker label for meaning, rendered on the symbol picker. */
#define TR_SYM_MEANING "Meaning"
/** Symbol-picker label for currently configured, rendered on the symbol picker. */
#define TR_SYM_CURRENTLY_CONFIGURED "Currently configured symbols"

/** @} */

/**
 * @name page_system.c
 * @{
 */
/** System page label for web admin login, rendered on the System page. */
#define TR_SYS_WEB_ADMIN_LOGIN "Web Admin Login"
/** System page note clarifying that a blank username only disables the password prompt, rendered on the System page. */
#define TR_SYS_WEB_ADMIN_LOGIN_NOTE                                                                                                                            \
    "Leaving the username blank disables the password prompt only. Same-origin requests are still required for every change made through this admin "          \
    "interface."
/** System page label for time, rendered on the System page. */
#define TR_SYS_TIME "Time"
/** System page label for sync ntp, rendered on the System page. */
#define TR_SYS_SYNC_NTP "Sync time via NTP"
/** System page label for ntp host, rendered on the System page. */
#define TR_SYS_NTP_HOST "NTP host (primary)"
/** System page label for ntp host2, rendered on the System page. */
#define TR_SYS_NTP_HOST2 "NTP host (fallback 2)"
/** System page label for ntp host3, rendered on the System page. */
#define TR_SYS_NTP_HOST3 "NTP host (fallback 3)"
/** System page label for ntp resync, rendered on the System page. */
#define TR_SYS_NTP_RESYNC "NTP resync interval (s, min 30)"
/** System page label for timezone, rendered on the System page. */
#define TR_SYS_TIMEZONE "Time zone (dashboard display only)"
/** System page label for digi path aliases, rendered on the System page. */
#define TR_SYS_DIGI_PATH_ALIASES "Digipeater Path Aliases"
/** System page label for path 1, rendered on the System page. */
#define TR_SYS_PATH_1 "Path 1"
/** System page label for path 2, rendered on the System page. */
#define TR_SYS_PATH_2 "Path 2"
/** System page label for path 3, rendered on the System page. */
#define TR_SYS_PATH_3 "Path 3"
/** System page label for path 4, rendered on the System page. */
#define TR_SYS_PATH_4 "Path 4"
/** System page label for confirm factory reset, rendered on the System page. */
#define TR_SYS_CONFIRM_FACTORY_RESET "Reset ALL settings to factory defaults?"
/** System page label for factory reset, rendered on the System page. */
#define TR_SYS_FACTORY_RESET "Factory Reset"

/** @} */

/**
 * @name page_tlm.c
 * @{
 */
/** Telemetry configurator label for avg, rendered on the Telemetry page. */
#define TR_TLM_AVG "Avg"
/** Telemetry configurator label for bit, rendered on the Telemetry page. */
#define TR_TLM_BIT "Bit"
/** @} */

/**
 * @name page_tlm.c: telemetry configurator
 * @{
 */
/** Telemetry configurator label for enable telemetry, rendered on the Telemetry page. */
#define TR_TLM_ENABLE_TELEMETRY "Enable Telemetry"
/** Telemetry configurator label for report params, rendered on the Telemetry page. */
#define TR_TLM_REPORT_PARAMS "Report Parameters"
/** Telemetry configurator label for path digis, rendered on the Telemetry page. */
#define TR_TLM_PATH_DIGIS "Path (digipeaters)"
/** Telemetry configurator label for destination, rendered on the Telemetry page. */
#define TR_TLM_DESTINATION "Destination"
/** Telemetry configurator label for auto inc seq, rendered on the Telemetry page. */
#define TR_TLM_AUTO_INC_SEQ "Auto-increment sequence"
/** Telemetry configurator label for analog field width, rendered on the Telemetry page. */
#define TR_TLM_ANALOG_FIELD_WIDTH "Analog field width"
/** Telemetry configurator label for fieldw 3digit, rendered on the Telemetry page. */
#define TR_TLM_FIELDW_3DIGIT "3-digit zero-padded (000-255, strict)"
/** Telemetry configurator label for fieldw auto, rendered on the Telemetry page. */
#define TR_TLM_FIELDW_AUTO "Minimal / as-needed (integers or decimals)"
/** Telemetry configurator label for omit trailing, rendered on the Telemetry page. */
#define TR_TLM_OMIT_TRAILING "Omit unused trailing channels"
/** Telemetry configurator label for trail comment, rendered on the Telemetry page. */
#define TR_TLM_TRAIL_COMMENT "Trailing comment (optional, after bits)"
/** Telemetry configurator label for comment tlm, rendered on the Telemetry page. */
#define TR_TLM_COMMENT_TLM "Also carry telemetry in position comment (APRS 1.2, |ss..|)"
/** Telemetry configurator label for analog count, rendered on the Telemetry page. */
#define TR_TLM_ANALOG_COUNT "Analog channels sent"
/** Telemetry configurator label for digital count, rendered on the Telemetry page. */
#define TR_TLM_DIGITAL_COUNT "Digital bits sent"
/** Telemetry configurator label for def messages, rendered on the Telemetry page. */
#define TR_TLM_DEF_MESSAGES "Definition Messages"
/** Telemetry configurator label for gen parm, rendered on the Telemetry page. */
#define TR_TLM_GEN_PARM "PARM - channel & bit names"
/** Telemetry configurator label for gen unit, rendered on the Telemetry page. */
#define TR_TLM_GEN_UNIT "UNIT - units / bit-state labels"
/** Telemetry configurator label for gen eqns, rendered on the Telemetry page. */
#define TR_TLM_GEN_EQNS "EQNS - scaling coefficients (A,B,C)"
/** Telemetry configurator label for gen bits, rendered on the Telemetry page. */
#define TR_TLM_GEN_BITS "BITS - bit sense + name"
/** Telemetry configurator label for analog legend, rendered on the Telemetry page. */
#define TR_TLM_ANALOG_LEGEND "Analog Channels (A1-A5)"
/** Telemetry configurator label for digital legend, rendered on the Telemetry page. */
#define TR_TLM_DIGITAL_LEGEND "Digital Channels (B1-B8)"
/** Telemetry configurator label for unit, rendered on the Telemetry page. */
#define TR_TLM_UNIT "Unit"
/** Telemetry configurator label for source, rendered on the Telemetry page. */
#define TR_TLM_SOURCE "Source"
/** Telemetry configurator label for rf, rendered on the Telemetry page. */
#define TR_TLM_RF "RF"
/** Telemetry configurator label for raw min, rendered on the Telemetry page. */
#define TR_TLM_RAW_MIN "Raw min"
/** Telemetry configurator label for raw max, rendered on the Telemetry page. */
#define TR_TLM_RAW_MAX "Raw max"
/** Telemetry configurator label for coef a, rendered on the Telemetry page. */
#define TR_TLM_COEF_A "A (quadratic)"
/** Telemetry configurator label for coef b, rendered on the Telemetry page. */
#define TR_TLM_COEF_B "B (linear / slope)"
/** Telemetry configurator label for coef c, rendered on the Telemetry page. */
#define TR_TLM_COEF_C "C (offset)"
/** Telemetry configurator label for decimals, rendered on the Telemetry page. */
#define TR_TLM_DECIMALS "Displayed decimals"
/** Telemetry configurator label for on state, rendered on the Telemetry page. */
#define TR_TLM_ON_STATE "On-state means"
/** Telemetry configurator label for sense, rendered on the Telemetry page. */
#define TR_TLM_SENSE "Sense"
/** Telemetry configurator label for label, rendered on the Telemetry page. */
#define TR_TLM_LABEL "Label"
/** Telemetry configurator label for calib wizard, rendered on the Telemetry page. */
#define TR_TLM_CALIB_WIZARD "2-point calibration wizard"
/** Telemetry configurator label for calib prompt x1, rendered on the Telemetry page. */
#define TR_TLM_CALIB_PROMPT_X1 "Raw reading #1 (x1):"
/** Telemetry configurator label for calib prompt y1, rendered on the Telemetry page. */
#define TR_TLM_CALIB_PROMPT_Y1 "Known real-world value at x1:"
/** Telemetry configurator label for calib prompt x2, rendered on the Telemetry page. */
#define TR_TLM_CALIB_PROMPT_X2 "Raw reading #2 (x2):"
/** Telemetry configurator label for calib prompt y2, rendered on the Telemetry page. */
#define TR_TLM_CALIB_PROMPT_Y2 "Known real-world value at x2:"
/** Telemetry configurator label for calib same x, rendered on the Telemetry page. */
#define TR_TLM_CALIB_SAME_X "x1 and x2 must differ."
/** Telemetry configurator label for calib cancelled, rendered on the Telemetry page. */
#define TR_TLM_CALIB_CANCELLED "Calibration cancelled: enter numeric values."

/** @} */

/**
 * @name page_radio.c
 * @{
 */
/** Radiomodem page label for audio hw title, rendered on the Radiomodem page. */
#define TR_RADIO_AUDIO_HW_TITLE "Audio hardware (compile-time)"
/** Radiomodem page label for audio hw info, rendered on the Radiomodem page. */
#define TR_RADIO_AUDIO_HW_INFO "<br>DAC out: GPIO%d<br>ADC in: GPIO%d<br>PTT pin: %s<br>PTT active-high: %s<br>ADC attenuation: %d<br>ADC: %d Hz<br>DAC: %d Hz"
/** Radiomodem page label for audio hw note, rendered on the Radiomodem page. */
#define TR_RADIO_AUDIO_HW_NOTE ""

/** @} */

/**
 * @name page_wireless.c
 * @{
 */
/** Wireless page label for mode legend, rendered on the Wireless page. */
#define TR_WIFI_MODE_LEGEND "WiFi Mode"
/** Wireless page label for station, rendered on the Wireless page. */
#define TR_WIFI_STATION "Station (STA)"
/** Wireless page label for access point, rendered on the Wireless page. */
#define TR_WIFI_ACCESS_POINT "Access Point (AP)"
/** Wireless page label for ap sta, rendered on the Wireless page. */
#define TR_WIFI_AP_STA "AP + STA"
/** Wireless page label for tx power, rendered on the Wireless page. */
#define TR_WIFI_TX_POWER "TX Power (0-20 dBm)"
/** Wireless page label for ap ssid, rendered on the Wireless page. */
#define TR_WIFI_AP_SSID "AP SSID"
/** Wireless page label for ap password, rendered on the Wireless page. */
#define TR_WIFI_AP_PASSWORD "AP Password"
/** Wireless page label for ap channel, rendered on the Wireless page. */
#define TR_WIFI_AP_CHANNEL "AP Channel"
/** Wireless page label for client legend, rendered on the Wireless page. */
#define TR_WIFI_CLIENT_LEGEND "WiFi Client #%d"
/** Caption of the "wifi scan" button, rendered on the Wireless page. */
#define TR_BTN_WIFI_SCAN "WIFI SCAN"
/** Wireless page label for ssid placeholder, rendered on the Wireless page. */
#define TR_WIFI_SSID_PLACEHOLDER "Network name (type it, or use WiFi Scan)"
/** Wireless page label for sta needs ssid, rendered on the Wireless page. */
#define TR_WIFI_STA_NEEDS_SSID                                                                                                                                 \
    "Saved, but this will NOT connect: Mode selects a station, yet no WiFi Client block has both 'Enable' ticked and an SSID filled in. Fix that and Save "    \
    "again."
/** Wireless page label for scanning, rendered on the Wireless page. */
#define TR_WIFI_SCANNING "Scanning..."
/** Wireless page label for scan failed, rendered on the Wireless page. */
#define TR_WIFI_SCAN_FAILED "Scan failed"

/** @} */

/**
 * @name page_wx.c
 * @{
 */
/** Weather page label for wind speed, rendered on the Weather page. */
#define TR_WX_WIND_SPEED "Wind Speed"
/** Weather page label for wind gust, rendered on the Weather page. */
#define TR_WX_WIND_GUST "Wind Gust"
/** Weather page label for wind direction, rendered on the Weather page. */
#define TR_WX_WIND_DIRECTION "Wind Direction"
/** Weather page label for temperature, rendered on the Weather page. */
#define TR_WX_TEMPERATURE "Temperature"
/** Weather page label for rain 1h, rendered on the Weather page. */
#define TR_WX_RAIN_1H "Rain 1h"
/** Weather page label for rain 24h, rendered on the Weather page. */
#define TR_WX_RAIN_24H "Rain 24h"
/** Weather page label for rain midnight, rendered on the Weather page. */
#define TR_WX_RAIN_MIDNIGHT "Rain since midnight"
/** Weather page label for humidity, rendered on the Weather page. */
#define TR_WX_HUMIDITY "Humidity"
/** Weather page label for pressure, rendered on the Weather page. */
#define TR_WX_PRESSURE "Pressure"
/** Weather page label for luminosity, rendered on the Weather page. */
#define TR_WX_LUMINOSITY "Luminosity"
/** Weather page label for snow, rendered on the Weather page. */
#define TR_WX_SNOW "Snow"
/** Weather page label for flood ft, rendered on the Weather page. */
#define TR_WX_FLOOD_FT "Flood Height (ft)"
/** Weather page label for flood m, rendered on the Weather page. */
#define TR_WX_FLOOD_M "Flood Height"
/** Weather page label for the raw rain counter, rendered on the Weather page. */
#define TR_WX_RAIN_RAW "Raw rain counter"
/** Weather page label for field, rendered on the Weather page. */
#define TR_WX_FIELD "WX Field"
/** Weather page label for channel, rendered on the Weather page. */
#define TR_WX_CHANNEL "Channel"
/** Weather page label for channel none, rendered on the Weather page. */
#define TR_WX_CHANNEL_NONE "(none)"
/** Weather page label for value, rendered on the Weather page. */
#define TR_WX_VALUE "Value"

/** @} */

/**
 * @name IGATE page additions (station symbol, path preset, timestamp, PHG, filters)
 * @{
 */
/** Form label for the "station symbol" field or fieldset, rendered on the IGate page. */
#define TR_F_STATION_SYMBOL "Station Symbol"
/** Form label for the "symbol table" field or fieldset, rendered on the IGate page. */
#define TR_F_SYMBOL_TABLE "Table"
/** Form label for the "symbol code" field or fieldset, rendered on the IGate page. */
#define TR_F_SYMBOL_CODE "Symbol"
/** Caption of the "pick symbol" button, rendered on the IGate page. */
#define TR_BTN_PICK_SYMBOL "..."
/** Symbol-picker label for pick hint, rendered on the IGate page. */
#define TR_SYM_PICK_HINT "Click icon for select symbol"
/** Form label for the "path" field or fieldset, rendered on the IGate page. */
#define TR_F_PATH "PATH"
/** Digipeat path preset label for direct, rendered on the IGate page. */
#define TR_PATH_DIRECT "Direct (no path)"
/** Digipeat path preset label for custom unset, rendered on the IGate page. */
#define TR_PATH_CUSTOM_UNSET "(not set)"
/** Digipeat path preset label for direct hint, rendered on the IGate page. */
#define TR_PATH_DIRECT_HINT "no digipeater path - only stations that hear you directly will receive it"
/** Digipeat path preset label for hop hint, rendered on the IGate page. */
#define TR_PATH_HOP_HINT "hop(s) via digipeater, encoded as an SSID suffix (short WIDEn-N form)"
/** Digipeat path preset label for custom hint, rendered on the IGate page. */
#define TR_PATH_CUSTOM_HINT "custom digipeater path configured on the System page"
/** Form label for the "time stamp" field or fieldset, rendered on the IGate page. */
#define TR_F_TIME_STAMP "Time Stamp"
/** Form label for the "tx channel" field or fieldset, rendered on the IGate page. */
#define TR_F_TX_CHANNEL "TX Channel"
/** Form label for the "phg section" field or fieldset, rendered on the IGate page. */
#define TR_F_PHG_SECTION "PHG"
/** Form label for the "enable phg" field or fieldset, rendered on the IGate page. */
#define TR_F_ENABLE_PHG "Enable PHG"
/** Form label for the "radio tx power" field or fieldset, rendered on the IGate and Digipeater pages. */
#define TR_F_RADIO_TX_POWER "Radio TX Power"
/** Form label for the "antenna gain" field or fieldset, rendered on the IGate and Digipeater pages. */
#define TR_F_ANTENNA_GAIN "Antenna Gain"
/** Form label for the "height m" field or fieldset, rendered on the IGate and Digipeater pages. */
#define TR_F_HEIGHT_M "Height (m)"
/** Form label for the "antenna direction" field or fieldset, rendered on the IGate and Digipeater pages. */
#define TR_F_ANTENNA_DIRECTION "Antenna/Direction"
/** Form label for the "phg text" field or fieldset, rendered on the IGate and Digipeater pages. */
#define TR_F_PHG_TEXT "PHG Text"
/** Form label for the "ext section" field or fieldset, rendered on the IGate and Digipeater pages. */
#define TR_F_EXT_SECTION "Data Extension"
/** Form label for the "enable ext" field or fieldset, rendered on the IGate and Digipeater pages. */
#define TR_F_ENABLE_EXT "Enable data extension"
/** Form label for the "ext type" field or fieldset, rendered on the IGate and Digipeater pages. */
#define TR_F_EXT_TYPE "Extension type"
/** Data-extension label for phg, rendered on the IGate and Digipeater pages. */
#define TR_EXT_PHG "PHG - power/height/gain/directivity"
/** Data-extension label for rng, rendered on the IGate and Digipeater pages. */
#define TR_EXT_RNG "RNG - pre-calculated radio range"
/** Data-extension label for dfs, rendered on the IGate and Digipeater pages. */
#define TR_EXT_DFS "DFS - omni-DF signal strength"
/** Data-extension label for df, rendered on the IGate and Digipeater pages. */
#define TR_EXT_DF "DF - bearing and NRQ report"
/** Explanatory note shown in the Data Extension fieldset when DF is selected but the station symbol is not the DF symbol, rendered on the IGate and Digipeater
 * pages. */
#define TR_NOTE_EXT_DF_SYMBOL                                                                                                                                  \
    "The DF report is only meaningful with the DF symbol (table '/', code '\\'), "                                                                             \
    "so with the symbol set above it is not transmitted."
/** Form label for the "ext range mi" field or fieldset, rendered on the IGate and Digipeater pages. */
#define TR_F_EXT_RANGE_MI "Radio range (miles)"
/** Form label for the "ext dfs strength" field or fieldset, rendered on the IGate and Digipeater pages. */
#define TR_F_EXT_DFS_STRENGTH "Signal strength (S-points, 0 = not heard)"
/** Form label for the "ext df bearing" field or fieldset, rendered on the IGate and Digipeater pages. */
#define TR_F_EXT_DF_BEARING "Signal bearing (degrees)"
/** Form label for the "ext df nrq n" field or fieldset, rendered on the IGate and Digipeater pages. */
#define TR_F_EXT_DF_NRQ_N "Hits per period (N, 0 = NRQ not meaningful)"
/** Form label for the "ext df nrq r" field or fieldset, rendered on the IGate and Digipeater pages. */
#define TR_F_EXT_DF_NRQ_R "Range code (R, range = 2^R miles)"
/** Form label for the "ext df nrq q" field or fieldset, rendered on the IGate and Digipeater pages. */
#define TR_F_EXT_DF_NRQ_Q "Bearing accuracy (Q, 9 = best)"
/** Form label for the "pos ambiguity" field or fieldset, rendered on the IGate page. */
#define TR_F_POS_AMBIGUITY "Position ambiguity"
/** Position-ambiguity selector entry: none, rendered on the IGate page. */
#define TR_AMB_NONE "Full precision"
/** Position-ambiguity selector entry: tenth, rendered on the IGate page. */
#define TR_AMB_TENTH "Nearest 1/10 minute"
/** Position-ambiguity selector entry: minute, rendered on the IGate page. */
#define TR_AMB_MINUTE "Nearest minute"
/** Position-ambiguity selector entry: ten minutes, rendered on the IGate page. */
#define TR_AMB_TEN_MINUTES "Nearest 10 minutes"
/** Position-ambiguity selector entry: degree, rendered on the IGate page. */
#define TR_AMB_DEGREE "Nearest degree"
/** Form label for the "status grid" field or fieldset, rendered on the IGate page. */
#define TR_F_STATUS_GRID "Maidenhead locator in status reports"
/** Form label for the "status timestamp" field or fieldset, rendered on the IGate page. */
#define TR_F_STATUS_TIMESTAMP "Zulu timestamp in status reports"
/** Form label for the "status beam" field or fieldset, rendered on the IGate page. */
#define TR_F_STATUS_BEAM "Beam heading in status reports (deg)"
/** Form label for the "status erp" field or fieldset, rendered on the IGate page. */
#define TR_F_STATUS_ERP "ERP in status reports (W)"
/** Form label for the "pos dao" field or fieldset, rendered on the IGate page. */
#define TR_F_POS_DAO "DAO precision extension in position reports"
/** Form label for the "no archive" field or fieldset, rendered on the Station page. */
#define TR_F_NO_ARCHIVE "Request APRS-IS not to archive my packets (!x!)"
/** Compass direction abbreviation: omni, rendered on the IGate and Digipeater pages. */
#define TR_DIR_OMNI "Omni"
/** Compass direction abbreviation: n, rendered on the IGate and Digipeater pages. */
#define TR_DIR_N "N"
/** Compass direction abbreviation: ne, rendered on the IGate and Digipeater pages. */
#define TR_DIR_NE "NE"
/** Compass direction abbreviation: e, rendered on the IGate and Digipeater pages. */
#define TR_DIR_E "E"
/** Compass direction abbreviation: se, rendered on the IGate and Digipeater pages. */
#define TR_DIR_SE "SE"
/** Compass direction abbreviation: s, rendered on the IGate and Digipeater pages. */
#define TR_DIR_S "S"
/** Compass direction abbreviation: sw, rendered on the IGate and Digipeater pages. */
#define TR_DIR_SW "SW"
/** Compass direction abbreviation: w, rendered on the IGate and Digipeater pages. */
#define TR_DIR_W "W"
/** Compass direction abbreviation: nw, rendered on the IGate and Digipeater pages. */
#define TR_DIR_NW "NW"
/** Form label for the "igate filter" field or fieldset, rendered on the IGate page. */
#define TR_F_IGATE_FILTER "IGate Filter"
/** Form label for the "filter rf2inet" field or fieldset, rendered on the IGate page. */
#define TR_F_FILTER_RF2INET "Filter RF to Internet"
/** Form label for the "filter inet2rf" field or fieldset, rendered on the IGate page. */
#define TR_F_FILTER_INET2RF "Filter Internet to RF"
/** APRS-IS filter editor label for message, rendered on the IGate page. */
#define TR_FILT_MESSAGE "Message"
/** APRS-IS filter editor label for status, rendered on the IGate page. */
#define TR_FILT_STATUS "Status"
/** APRS-IS filter editor label for telemetry, rendered on the IGate page. */
#define TR_FILT_TELEMETRY "Telemetry"
/** APRS-IS filter editor label for weather, rendered on the IGate page. */
#define TR_FILT_WEATHER "Weather"
/** APRS-IS filter editor label for object, rendered on the IGate page. */
#define TR_FILT_OBJECT "Object"
/** APRS-IS filter editor label for item, rendered on the IGate page. */
#define TR_FILT_ITEM "Item"
/** APRS-IS filter editor label for buoy, rendered on the IGate page. */
#define TR_FILT_BUOY "Buoy"
/** APRS-IS filter editor label for position, rendered on the IGate page. */
#define TR_FILT_POSITION "Position"
/** APRS-IS filter editor label for the payload kinds that share one bit (capabilities, user-defined, direction finding, locator beacons, map feature), rendered
 * on the IGate page. */
#define TR_FILT_OTHER "Other"

/** Form label for the "callsign filter" field or fieldset, rendered on the IGate page. */
#define TR_F_CALLSIGN_FILTER "Callsign Filter"
/** Form label for the "budlist mode rf2inet" field or fieldset, rendered on the IGate page. */
#define TR_F_BUDLIST_MODE_RF2INET "RF to Internet Mode"
/** Form label for the "budlist mode inet2rf" field or fieldset, rendered on the IGate page. */
#define TR_F_BUDLIST_MODE_INET2RF "Internet to RF Mode"
/** Buddy-list editor label for off, rendered on the IGate page. */
#define TR_BUDLIST_OFF "Off"
/** Buddy-list editor label for whitelist, rendered on the IGate page. */
#define TR_BUDLIST_WHITELIST "Whitelist"
/** Buddy-list editor label for blacklist, rendered on the IGate page. */
#define TR_BUDLIST_BLACKLIST "Blacklist"
/** Form label for the "budlist call" field or fieldset, rendered on the IGate page. */
#define TR_F_BUDLIST_CALL "Callsign"
/** Explanatory note shown beside the budlist setting, rendered on the IGate page. */
#define TR_NOTE_BUDLIST "Shared callsign list, up to 8 entries. Whitelist: only listed calls pass. Blacklist: listed calls are blocked."

/** Form label for the "range filter en" field or fieldset, rendered on the IGate page. */
#define TR_F_RANGE_FILTER_EN "Enable range filter"
/** Form label for the "range km" field or fieldset, rendered on the IGate page. */
#define TR_F_RANGE_KM "Max distance (km, 0 = unlimited)"
/** Form label for the "prefix filter en" field or fieldset, rendered on the IGate page. */
#define TR_F_PREFIX_FILTER_EN "Enable callsign-prefix filter"
/** Form label for the "prefixes" field or fieldset, rendered on the IGate page. */
#define TR_F_PREFIXES "Allowed prefixes (comma-separated)"
/** Explanatory note shown beside the range prefix setting, rendered on the IGate page. */
#define TR_NOTE_RANGE_PREFIX                                                                                                                                   \
    "Local gate applied only to RF -> Internet, independent of the payload-type filter above. Range is measured from My Station's position; packets whose "    \
    "position can't be decoded are not affected by the range filter."

/** Form label for the "3rdparty unwrap en" field or fieldset, rendered on the IGate page. */
#define TR_F_3RDPARTY_UNWRAP_EN "Relay whitelisted third-party (}) traffic"
/** Explanatory note shown beside the 3rdparty unwrap setting, rendered on the IGate page. */
#define TR_NOTE_3RDPARTY_UNWRAP                                                                                                                                \
    "Off by default. Only takes effect when the Internet to RF Callsign Filter above is set to Whitelist: a third-party-wrapped packet is only ever "          \
    "unwrapped and relayed if its inner source callsign is itself on the whitelist. Only enable if you trust and have whitelisted the specific source - "      \
    "re-gating third-party traffic without this restriction is the most common cause of IGate loops."

/** Form label for the "satgate" field or fieldset, rendered on the IGate page. */
#define TR_F_SATGATE "Satellite Gate List"
/** Form label for the "satgate call" field or fieldset, rendered on the IGate page. */
#define TR_F_SATGATE_CALL "Satellite Callsign"
/** Explanatory note shown beside the satgate setting, rendered on the IGate page. */
#define TR_NOTE_SATGATE                                                                                                                                        \
    "Callsigns of satellite/ISS digipeaters (e.g. ISS, PSAT). A frame routed through one of these is only gated to APRS-IS if the digipeater's path entry "    \
    "is actually marked used. Up to 8 entries; leave a slot blank to disable it."

/** Form label for the "dup cache" field or fieldset, rendered on the IGate page. */
#define TR_F_DUP_CACHE "Duplicate Suppression"
/** Form label for the "dup cache size" field or fieldset, rendered on the IGate page. */
#define TR_F_DUP_CACHE_SIZE "Cache Size (entries)"
/** Form label for the "dup cache timeout ms" field or fieldset, rendered on the IGate page. */
#define TR_F_DUP_CACHE_TIMEOUT_MS "Suppression Window (ms)"
/** Explanatory note shown beside the dup cache setting, rendered on the IGate page. */
#define TR_NOTE_DUP_CACHE                                                                                                                                      \
    "Shared by the IGate and the Digipeater to suppress repeated copies of the same frame. A busy digipeater on a congested frequency may need a larger "      \
    "cache; a sparse rural IGate may prefer a shorter window."

/** Form label for the "msg gating" field or fieldset, rendered on the IGate page. */
#define TR_F_MSG_GATING "Message Gating (Internet to RF)"
/** Form label for the "msg gate en" field or fieldset, rendered on the IGate page. */
#define TR_F_MSG_GATE_EN "Apply message gating criteria"
/** Form label for the "msg local window s" field or fieldset, rendered on the IGate page. */
#define TR_F_MSG_LOCAL_WINDOW_S "Heard-locally window (s)"
/** Form label for the "msg max hops" field or fieldset, rendered on the IGate page. */
#define TR_F_MSG_MAX_HOPS "Addressee hop limit (0 = direct only)"
/** Explanatory note shown beside the msg gating setting, rendered on the IGate page. */
#define TR_NOTE_MSG_GATING                                                                                                                                     \
    "A message read from APRS-IS is put on the air only when its addressee was heard on RF inside the window and over no more than the hop limit, its "        \
    "sender was not heard on RF, the sender's header carries no TCPXX/NOGATE/RFONLY, and the addressee is not itself on the Internet. The next position "      \
    "report seen for that addressee is gated once too, so it can be plotted. The hop limit is what keeps the gateway from transmitting to a station it "       \
    "hears through distant digipeaters but cannot reach back; set it to the number of hops your own transmit path travels. Switching this off transmits "      \
    "every message the type filter allows, to addressees anywhere in the world."

/** Form label for the "digi aliases" field or fieldset, rendered on the IGate page. */
#define TR_F_DIGI_ALIASES "n-N Path Aliases"
/** Form label for the "digi alias" field or fieldset, rendered on the IGate page. */
#define TR_F_DIGI_ALIAS "Alias"
/** Form label for the "digi max n" field or fieldset, rendered on the IGate page. */
#define TR_F_DIGI_MAX_N "Max N"
/** Form label for the "digi alias mode" field or fieldset, rendered on the IGate page. */
#define TR_F_DIGI_ALIAS_MODE "Mode"
/** Form label for the "digi fillin only" field or fieldset, rendered on the IGate page. */
#define TR_F_DIGI_FILLIN_ONLY "Fill-in digipeater (single hop only)"
/** Form label for the "digi trap action" field or fieldset, rendered on the IGate page. */
#define TR_F_DIGI_TRAP_ACTION "Hop count above Max N"
/** Digipeater page label for trap clamp, rendered on the IGate page. */
#define TR_DIGI_TRAP_CLAMP "Clamp to Max N"
/** Digipeater page label for trap drop, rendered on the IGate page. */
#define TR_DIGI_TRAP_DROP "Drop the frame"
/** Digipeater page label for mode off, rendered on the IGate page. */
#define TR_DIGI_MODE_OFF "Off"
/** Digipeater page label for mode trace, rendered on the IGate page. */
#define TR_DIGI_MODE_TRACE "Trace (insert callsign)"
/** Digipeater page label for mode flood, rendered on the IGate page. */
#define TR_DIGI_MODE_FLOOD "Flood (no callsign)"
/** Form label for the "digi preempt" field or fieldset, rendered on the IGate page. */
#define TR_F_DIGI_PREEMPT "Explicit routes naming this station"
/** Digipeater page label for preempt off, rendered on the IGate page. */
#define TR_DIGI_PREEMPT_OFF "Off (first unused address only)"
/** Digipeater page label for preempt mark, rendered on the IGate page. */
#define TR_DIGI_PREEMPT_MARK "Serve now, keep the skipped addresses"
/** Digipeater page label for preempt drop, rendered on the IGate page. */
#define TR_DIGI_PREEMPT_DROP "Serve now, discard the skipped addresses"
/** Form label for the "digi dest ssid" field or fieldset, rendered on the IGate page. */
#define TR_F_DIGI_DEST_SSID "Digipeat by destination SSID (legacy)"
/** Explanatory note shown beside the digi aliases setting, rendered on the IGate page. */
#define TR_NOTE_DIGI_ALIASES                                                                                                                                   \
    "The only aliases this digipeater honours. Write each one without its SSID; '#' matches a single digit, so 'WIDE#' covers the whole WIDEn family. Rows "   \
    "are tried in order and the first match wins. Trace inserts this station's callsign so every hop can be identified afterwards, which is what WIDEn-N "     \
    "requires; flood leaves no trace and suits only a regional alias run that way on purpose."

/** Explanatory note shown beside the digi preempt setting, rendered on the IGate page. */
#define TR_NOTE_DIGI_PREEMPT                                                                                                                                   \
    "Off is the safe default. When it is on, the path is scanned from its first unused address to the end for this station's own callsign or for one of the "  \
    "alias rows above that is not an n-N family name, and a match found further down is served at once instead of waiting for the addresses in front of it. "  \
    "That is what carries an explicit route such as WIDE1-1,CITYA,WIDE2-1,CITYB, which loads the channel far less than a WIDEn-N flood. Keeping the skipped "  \
    "addresses leaves the requested route visible; discarding them puts the shortest remaining path on the air. Generic n-N aliases are never claimed this "   \
    "way in either mode."

/** Explanatory note shown beside the digi dest ssid setting, rendered on the IGate page. */
#define TR_NOTE_DIGI_DEST_SSID                                                                                                                                 \
    "Off by default. When on, a frame whose AX.25 destination SSID is 1 to 7 is repeated on the strength of that SSID alone, before the alias table above is " \
    "consulted, so the path the originating station asked for is ignored. Turn it on only for a legacy neighbour that still routes this way."

/** Symbol-picker label for icon, rendered on the IGate page. */
#define TR_SYM_ICON "Icon"
/** Symbol-picker label for quick pick, rendered on the IGate page. */
#define TR_SYM_QUICK_PICK "Quick Pick"
/** Symbol-picker label for primary table, rendered on the IGate page. */
#define TR_SYM_PRIMARY_TABLE "Primary Table ( / )"
/** Symbol-picker label for alternate table, rendered on the IGate page. */
#define TR_SYM_ALTERNATE_TABLE "Alternate Table ( \\ )"
/** Symbol-picker label for tracker, rendered on the IGate page. */
#define TR_SYM_TRACKER "Tracker:"

/** @} */

/**
 * @name GPS page
 * @{
 */
/** Page title of the GPS page, rendered on the GPS page. */
#define TR_F_GPS "GPS Receiver"
/** Fieldset legend for the receiver enable block, rendered on the GPS page. */
#define TR_GPS_FS_SERVICE "GPS Receiver"
/** Label of the receiver enable checkbox, rendered on the GPS page. */
#define TR_GPS_ENABLE "Enable GPS Receiver"
/** Fieldset legend for the receiver status block, rendered on the GPS page. */
#define TR_GPS_FS_STATUS "Receiver Status"
/** Fieldset legend for the position block, rendered on the GPS page. */
#define TR_GPS_FS_POSITION "Position"
/** Fieldset legend for the motion block, rendered on the GPS page. */
#define TR_GPS_FS_MOTION "Motion"
/** Fieldset legend for the date/time block, rendered on the GPS page. */
#define TR_GPS_FS_TIME "Date and Time (UTC)"
/** Fieldset legend for the satellites/accuracy block, rendered on the GPS page. */
#define TR_GPS_FS_SATELLITES "Satellites and Accuracy"
/** Fieldset legend for the serial link statistics block, rendered on the GPS page. */
#define TR_GPS_FS_LINK "Serial Link"
/** Fieldset legend for the compile-time wiring block, rendered on the GPS page. */
#define TR_GPS_FS_WIRING "Wiring (compile-time)"
/** Row label for the coloured module-status badge summarising the serial link, rendered on the GPS page. */
#define TR_GPS_MODULE_STATUS "Module Status"
/** Row label for the receiver link state, rendered on the GPS page. */
#define TR_GPS_LINK "Link"
/** Row label for the navigation status reported by RMC, rendered on the GPS page. */
#define TR_GPS_NAV_STATUS "Navigation Status"
/** Row label for the fix quality reported by GGA, rendered on the GPS page. */
#define TR_GPS_FIX_QUALITY "Fix Quality"
/** Row label for the fix mode reported by GSA, rendered on the GPS page. */
#define TR_GPS_FIX_MODE "Fix Mode"
/** Row label for latitude, rendered on the GPS page. */
#define TR_GPS_LATITUDE "Latitude"
/** Row label for longitude, rendered on the GPS page. */
#define TR_GPS_LONGITUDE "Longitude"
/** Row label for altitude above mean sea level, rendered on the GPS page. */
#define TR_GPS_ALTITUDE "Altitude (MSL)"
/** Row label for geoid separation, rendered on the GPS page. */
#define TR_GPS_GEOID "Geoid Separation"
/** Row label for ground speed, rendered on the GPS page. */
#define TR_GPS_SPEED "Ground Speed"
/** Row label for course over ground, rendered on the GPS page. */
#define TR_GPS_COURSE "Course (true)"
/** Row label for magnetic variation, rendered on the GPS page. */
#define TR_GPS_MAGVAR "Magnetic Variation"
/** Row label for the UTC date of the fix, rendered on the GPS page. */
#define TR_GPS_UTC_DATE "Date"
/** Row label for the UTC time of the fix, rendered on the GPS page. */
#define TR_GPS_UTC_TIME "Time"
/** Row label for the satellite count used in the solution, rendered on the GPS page. */
#define TR_GPS_SATS_USED "Satellites Used"
/** Row label for the satellite count in view, rendered on the GPS page. */
#define TR_GPS_SATS_IN_VIEW "Satellites in View"
/** Row label for horizontal dilution of precision, rendered on the GPS page. */
#define TR_GPS_HDOP "HDOP (horizontal)"
/** Row label for position dilution of precision, rendered on the GPS page. */
#define TR_GPS_PDOP "PDOP (position)"
/** Row label for vertical dilution of precision, rendered on the GPS page. */
#define TR_GPS_VDOP "VDOP (vertical)"
/** Row label for the count of sentences whose checksum verified, rendered on the GPS page. */
#define TR_GPS_SENTENCES_OK "Sentences Accepted"
/** Row label for the count of sentences discarded on a checksum error, rendered on the GPS page. */
#define TR_GPS_SENTENCES_BAD "Sentences Discarded"
/** Row label for the time since the last valid sentence, rendered on the GPS page. */
#define TR_GPS_LINK_AGE "Since Last Sentence"
/** Row label for the time since the last position fix, rendered on the GPS page. */
#define TR_GPS_FIX_AGE "Since Last Fix"
/** Row label for the UART port number the receiver is wired to, rendered on the GPS page. */
#define TR_GPS_PORT "Serial Port"
/** Row label for the ESP32 receive pin, rendered on the GPS page. */
#define TR_GPS_RX_PIN "Receive Pin (module TX)"
/** Row label for the ESP32 transmit pin, rendered on the GPS page. */
#define TR_GPS_TX_PIN "Transmit Pin (module RX)"
/** Row label for the serial line rate, rendered on the GPS page. */
#define TR_GPS_BAUD "Line Rate"
/** Link state shown while sentences are arriving, rendered on the GPS page. */
#define TR_GPS_LINK_RECEIVING "Receiving"
/** Link state shown when the receiver has gone silent, rendered on the GPS page. */
#define TR_GPS_LINK_SILENT "No data from receiver"
/** Module-status badge text shown when the receiver is switched off or its UART failed to come up, rendered on the GPS page. */
#define TR_GPS_STATUS_DISABLED "Disabled"
/** Module-status badge text shown when the receiver is enabled but no sentence has arrived within the link timeout, rendered on the GPS page. */
#define TR_GPS_STATUS_NO_LINK "No data (check wiring)"
/** Module-status badge text shown when sentences are arriving but no valid fix has been reported yet, rendered on the GPS page. */
#define TR_GPS_STATUS_SEARCHING "Searching (no fix)"
/** Module-status badge text shown when sentences are arriving and the last navigation solution is valid, rendered on the GPS page. */
#define TR_GPS_STATUS_FIX_OK "Fix OK"
/** Navigation status shown when RMC reports an active solution, rendered on the GPS page. */
#define TR_GPS_NAV_ACTIVE "Active"
/** Navigation status shown when RMC reports a warning, rendered on the GPS page. */
#define TR_GPS_NAV_WARNING "Warning (no valid fix)"
/** Fix quality shown when the receiver has no fix, rendered on the GPS page. */
#define TR_GPS_Q_NONE "No fix"
/** Fix quality shown for an autonomous fix, rendered on the GPS page. */
#define TR_GPS_Q_GPS "GPS (autonomous)"
/** Fix quality shown for a differentially corrected fix, rendered on the GPS page. */
#define TR_GPS_Q_DGPS "DGPS (differential)"
/** Fix quality shown for a precise positioning service fix, rendered on the GPS page. */
#define TR_GPS_Q_PPS "PPS"
/** Fix quality shown for a fixed-ambiguity RTK solution, rendered on the GPS page. */
#define TR_GPS_Q_RTK "RTK (fixed)"
/** Fix quality shown for a float RTK solution, rendered on the GPS page. */
#define TR_GPS_Q_RTK_FLOAT "RTK (float)"
/** Fix quality shown for a dead-reckoning estimate, rendered on the GPS page. */
#define TR_GPS_Q_ESTIMATED "Estimated (dead reckoning)"
/** Fix quality shown for manual input mode, rendered on the GPS page. */
#define TR_GPS_Q_MANUAL "Manual input"
/** Fix quality shown for simulation mode, rendered on the GPS page. */
#define TR_GPS_Q_SIMULATED "Simulated"
/** Fix mode shown when no solution has been computed, rendered on the GPS page. */
#define TR_GPS_M_NOFIX "No fix"
/** Fix mode shown for a horizontal-only solution, rendered on the GPS page. */
#define TR_GPS_M_2D "2D (horizontal only)"
/** Fix mode shown for a solution including altitude, rendered on the GPS page. */
#define TR_GPS_M_3D "3D (with altitude)"

/** Sidebar menu entry for the BrandMeister page. */
#define TR_MENU_BM "BrandMeister"
/** Page title of the BrandMeister interconnect page. */
#define TR_F_BM "BrandMeister"
/** Fieldset legend for the interconnect enable switch, rendered on the BrandMeister page. */
#define TR_BM_FS_SERVICE "BrandMeister Interconnect"
/** Form label for the interconnect enable switch, rendered on the BrandMeister page. */
#define TR_BM_ENABLE "Enable BrandMeister interconnect"
/** Explanatory note shown beside the interconnect enable switch, rendered on the BrandMeister page. */
#define TR_BM_NOTE_SERVICE                                                                                                                                     \
    "BrandMeister's APRS side is an APRS-IS client: each master gates DMR-sourced traffic onto APRS-IS as ordinary packets. This station "                     \
    "therefore needs no DMR connection and no BrandMeister account - the interconnect rides the IGate's existing APRS-IS session. With this "                  \
    "switch off no line is classified and message routing is untouched."
/** Fieldset legend for the worldwide monitor subscription, rendered on the BrandMeister page. */
#define TR_BM_FS_MONITOR "Worldwide Monitor"
/** Form label for the worldwide monitor switch, rendered on the BrandMeister page. */
#define TR_BM_MONITOR "Subscribe to worldwide BrandMeister traffic"
/** Explanatory note shown beside the worldwide monitor switch, rendered on the BrandMeister page. */
#define TR_BM_NOTE_MONITOR                                                                                                                                     \
    "APRS-IS server filter terms are OR'd, never AND'd, so this subscription cannot be narrowed to your area by the server. It is refused "                    \
    "while Internet to RF gating is on and the Internet to RF range filter on the IGate page is off, because nothing else would stand between a "              \
    "worldwide feed and your transmitter."
/** Lead-in to the server filter term the monitor subscription needs, rendered on the BrandMeister page. */
#define TR_BM_NOTE_FILTER_TERM "Add this term to the IGate page's server filter to receive the subscription:"
/** Warning shown once when the monitor switch was refused for want of the range gate, rendered on the BrandMeister page. */
#define TR_BM_WARN_NEEDS_RANGE "Refused: enable the Internet to RF range filter on the IGate page first."
/** Fieldset legend for message routing, rendered on the BrandMeister page. */
#define TR_BM_FS_MESSAGING "Messaging"
/** Form label for the Internet-only message routing switch, rendered on the BrandMeister page. */
#define TR_BM_MSG_INET_ONLY "Send messages to BrandMeister stations over the Internet only"
/** Explanatory note shown beside the Internet-only message routing switch, rendered on the BrandMeister page. */
#define TR_BM_NOTE_MSG_INET_ONLY                                                                                                                               \
    "A station last heard as BrandMeister traffic is on the network, not on the local channel, so an RF copy of a message addressed to it is "                 \
    "airtime spent on a receiver that is not there. This never adds the Internet leg: with \"Send to Internet\" off on the Message page, nothing "             \
    "is sent at all."
/** Note on delivery not being guaranteed, rendered on the BrandMeister page. */
#define TR_BM_NOTE_DELIVERY                                                                                                                                    \
    "Delivery is not guaranteed and failure is silent: each BrandMeister master applies its own pattern to the addressee of incoming messages, "               \
    "which this station cannot see. A message that is filtered out simply produces no acknowledgement. The addressee must also be the callsign "               \
    "associated with the DMR ID in the recipient's SelfCare."
/** Fieldset legend for the gateway callsign list, rendered on the BrandMeister page. */
#define TR_BM_FS_GATEWAYS "Gateway Callsigns"
/** Form label prefix for one gateway callsign slot, rendered on the BrandMeister page. */
#define TR_BM_GATEWAY "Gateway"
/** Explanatory note shown beside the gateway callsign list, rendered on the BrandMeister page. */
#define TR_BM_NOTE_GATEWAYS                                                                                                                                    \
    "Optional. Matched against the entry station that follows the q construct, for an operator who wants only their own master's traffic "                     \
    "marked. A trailing * matches by prefix. Leave empty to recognise BrandMeister traffic by its APBMxx destination and its DMR path element "                \
    "alone, which needs no configuration."
/** Fieldset legend for the read-only status table, rendered on the BrandMeister page. */
#define TR_BM_FS_STATUS "Status"
/** Status row label for the interconnect switch, rendered on the BrandMeister page. */
#define TR_BM_ST_SERVICE "Interconnect"
/** Status row label for the monitor term in the server filter, rendered on the BrandMeister page. */
#define TR_BM_ST_FILTER_TERM "Monitor term in server filter"
/** Status row label for the Internet to RF range gate, rendered on the BrandMeister page. */
#define TR_BM_ST_RANGE_GATE "Internet to RF range filter"
/** Status row label for the count of BrandMeister stations heard, rendered on the BrandMeister page. */
#define TR_BM_ST_STATIONS "BrandMeister stations heard"
/** Status row value for an enabled service, rendered on the BrandMeister page. */
#define TR_BM_ST_ON "Enabled"
/** Status row value for a disabled service, rendered on the BrandMeister page. */
#define TR_BM_ST_OFF "Disabled"
/** Status row value for a filter term that is present, rendered on the BrandMeister page. */
#define TR_BM_ST_PRESENT "Present"
/** Status row value for a filter term that is missing, rendered on the BrandMeister page. */
#define TR_BM_ST_ABSENT "Absent"
/** Status row value for a range gate that governs nothing, rendered on the BrandMeister page. */
#define TR_BM_ST_GATE_NA "Not applicable (Internet to RF gating is off)"
/** Status row value for a range gate that is off, rendered on the BrandMeister page. */
#define TR_BM_ST_GATE_OFF "Off"
/** Explanatory note shown under the status table, rendered on the BrandMeister page. */
#define TR_BM_NOTE_STATUS                                                                                                                                      \
    "The last three rows are governed by the IGate page. BrandMeister stations are counted in the LAST HEARD table, where they carry a BM prefix."
/** Explanatory note shown beside the Internet to RF range gate, rendered on the IGate page. */
#define TR_NOTE_INET2RF_RANGE                                                                                                                                  \
    "Local gate applied only to Internet -> RF, independent of the payload-type filter above. Range is measured from My Station's position; lines "            \
    "whose position can't be decoded are not affected. Required before any worldwide subscription may be gated to the transmitter."

/** @} */

/**
 * @name Telegram page
 * @{
 */
/** Page title of the Telegram page, rendered on the Telegram page. */
#define TR_F_TELEGRAM "Telegram Bot"
/** Fieldset legend for the bot enable block, rendered on the Telegram page. */
#define TR_TG_FS_SERVICE "Telegram Bot"
/** Label of the bot enable checkbox, rendered on the Telegram page. */
#define TR_TG_ENABLE "Enable Telegram Bot"
/** Explanatory note shown under the enable switch, rendered on the Telegram page. */
#define TR_TG_NOTE_SERVICE                                                                                                                                     \
    "With this off nothing connects to Telegram and no polling task runs. Turning it on or off takes effect immediately, without a reboot. The bot needs a "   \
    "working Internet connection and enough free heap for a TLS session."
/** Fieldset legend for the credentials block, rendered on the Telegram page. */
#define TR_TG_FS_BOT "Credentials"
/** Label of the bot token field, rendered on the Telegram page. */
#define TR_TG_TOKEN "Bot Token"
/** Label of the administrator identifier field, rendered on the Telegram page. */
#define TR_TG_ADMIN_ID "Administrator ID"
/** Explanatory note shown under the credentials, rendered on the Telegram page. */
#define TR_TG_NOTE_ADMIN                                                                                                                                       \
    "The token is issued by @BotFather. The administrator identifier is a number, not a user name; send any command to the bot from your own account and it "  \
    "answers with that number, which is also written to the log. Leave it at 0 to add no administrator here."
/** Warning shown under the credentials when the bot is enabled with no administrator identifier, rendered on the Telegram page. */
#define TR_TG_WARN_NO_ADMIN                                                                                                                                    \
    "The bot is enabled but no administrator identifier is set. Nobody holds administrator rights, and unless an authorized user is listed below the bot "     \
    "turns every sender away. Send it any command from your own account: the refusal it answers with carries your numeric identifier, which is what goes in "  \
    "the field above."
/** Fieldset legend for the Mini App block, rendered on the Telegram page. */
#define TR_TG_FS_MINIAPP "Mini App"
/** Label of the Mini App address field, rendered on the Telegram page. */
#define TR_TG_MINIAPP_URL "Mini App Address"
/** Explanatory note shown under the Mini App address, rendered on the Telegram page. */
#define TR_TG_NOTE_MINIAPP "The HTTPS address of a Telegram Mini App the bot's menu button opens. Leave it empty to run the bot with no Mini App button."
/** Fieldset legend for the authorized-users block, rendered on the Telegram page. */
#define TR_TG_FS_USERS "Authorized Users"
/** Explanatory note shown above the authorized-users table, rendered on the Telegram page. */
#define TR_TG_NOTE_USERS                                                                                                                                       \
    "Up to 8 users, in addition to the administrator, who may talk to the bot as themselves rather than being turned away as unauthorized. The bot answers a " \
    "command from an account it does not know with that account's identifier, which is the number to enter here; an already authorized account reads it "      \
    "with /whoami. An empty identifier leaves the slot unused."
/** Fieldset legend for the allowed-group-chats block, rendered on the Telegram page. */
#define TR_TG_FS_CHATS "Allowed Group Chats"
/** Explanatory note shown above the allowed-group-chats table, rendered on the Telegram page. */
#define TR_TG_NOTE_CHATS                                                                                                                                       \
    "Up to 4 group chats the bot is allowed to answer in. A supergroup identifier is a large negative number; send /whoami to the bot from inside the group "  \
    "to read it off. An empty identifier leaves the slot unused."
/** Legend format ("User %d") for one authorized-user accordion card, rendered on the Telegram page. */
#define TR_TG_F_USER_FMT "User %d"
/** Legend format ("Chat %d") for one allowed-group-chat accordion card, rendered on the Telegram page. */
#define TR_TG_F_CHAT_FMT "Chat %d"
/** Label of one entry's identifier field, rendered on the Telegram page. */
#define TR_TG_F_PEER_ID "Identifier"
/** Label of one entry's display-name field, rendered on the Telegram page. */
#define TR_TG_F_PEER_NAME "Name"
/** Fieldset legend for the connection status block, rendered on the Telegram page. */
#define TR_TG_FS_STATUS "Connection Status"
/** Status row label for the coarse state, rendered on the Telegram page. */
#define TR_TG_ST_STATE "State"
/** Status row label for the reason behind the state, rendered on the Telegram page. */
#define TR_TG_ST_REASON "Diagnosis"
/** Status row label for the untranslated detail, rendered on the Telegram page. */
#define TR_TG_ST_DETAIL "Detail"
/** Status row label for the bot user name, rendered on the Telegram page. */
#define TR_TG_ST_BOT "Bot"
/** Status row label for the time the service has been polling, rendered on the Telegram page. */
#define TR_TG_ST_UPTIME "Running For"
/** Status row label for the count of decoded updates, rendered on the Telegram page. */
#define TR_TG_ST_UPDATES "Updates Received"
/** Status row label for the count of dispatched commands, rendered on the Telegram page. */
#define TR_TG_ST_COMMANDS "Commands Handled"
/** Status row label for the count of accepted outgoing messages, rendered on the Telegram page. */
#define TR_TG_ST_SENT "Messages Sent"
/** Status row label for the count of updates from unauthorized senders, rendered on the Telegram page. */
#define TR_TG_ST_REJECTED "Unauthorized Rejected"
/** Status row label for the consecutive polling failure count, rendered on the Telegram page. */
#define TR_TG_ST_POLL_ERRORS "Consecutive Poll Errors"
/** Explanatory note shown under the status table, rendered on the Telegram page. */
#define TR_TG_NOTE_STATUS                                                                                                                                      \
    "Refreshed every two seconds. The counters are reset each time the service starts. The detail row carries a file path, an ESP-IDF error name or the "      \
    "wording Telegram itself returned, and is shown untranslated on purpose."
/** Note naming the file the whole configuration lives in, rendered on the Telegram page. */
#define TR_TG_NOTE_FILE "Everything above is stored in this file, which can also be downloaded, edited and uploaded again from the File Storage page:"
/** Coarse state shown while the bot is switched off, rendered on the Telegram page. */
#define TR_TG_STATE_DISABLED "Disabled"
/** Coarse state shown while bring-up is in progress, rendered on the Telegram page. */
#define TR_TG_STATE_STARTING "Starting"
/** Coarse state shown while the bot is polling Telegram, rendered on the Telegram page. */
#define TR_TG_STATE_RUNNING "Running"
/** Coarse state shown when bring-up stopped at a fault, rendered on the Telegram page. */
#define TR_TG_STATE_ERROR "Error"
/** Diagnosis shown when the switch is off, rendered on the Telegram page. */
#define TR_TG_R_DISABLED "The bot is switched off on this page."
/** Diagnosis shown when the settings file is absent, rendered on the Telegram page. */
#define TR_TG_R_FILE_MISSING "The settings file is not on the storage partition. Save this page once to create it, or upload it from the File Storage page."
/** Diagnosis shown when the settings file does not parse, rendered on the Telegram page. */
#define TR_TG_R_FILE_CORRUPT                                                                                                                                   \
    "The settings file is not valid JSON. It has been left untouched so it can be examined: download it from the File Storage page, correct it and upload it " \
    "again, or save this page to overwrite it."
/** Diagnosis shown when the settings file could not be read into memory, rendered on the Telegram page. */
#define TR_TG_R_FILE_UNREADABLE                                                                                                                                \
    "The settings file could not be read into memory. The file is probably intact and the heap was momentarily exhausted; it has deliberately not been "       \
    "overwritten. Restart the station and look at the free heap on the dashboard."
/** Diagnosis shown when no token is configured, rendered on the Telegram page. */
#define TR_TG_R_NO_TOKEN "No bot token is configured. Create a bot with @BotFather, then paste the token it gives you into the field above."
/** Diagnosis shown when the token is not of the expected shape, rendered on the Telegram page. */
#define TR_TG_R_TOKEN_MALFORMED                                                                                                                                \
    "The bot token is not of the form <numbers>:<secret>. It was most likely pasted incomplete, wrapped over two lines, or copied together with the "          \
    "surrounding quotation marks. Paste it again from @BotFather."
/** Diagnosis shown when the root certificate file is absent, rendered on the Telegram page. */
#define TR_TG_R_CERT_MISSING                                                                                                                                   \
    "The root certificate that validates api.telegram.org is not on the storage partition. Upload it, as a PEM file, to the path shown in the detail row "     \
    "using the File Storage page. Without it no TLS connection can be established."
/** Diagnosis shown when the root certificate file is unusable, rendered on the Telegram page. */
#define TR_TG_R_CERT_INVALID                                                                                                                                   \
    "The root certificate file is empty, larger than this firmware accepts, or does not hold a PEM certificate. Upload a plain PEM file, beginning with the "  \
    "BEGIN CERTIFICATE line, from the File Storage page."
/** Diagnosis shown while there is no route to the Internet, rendered on the Telegram page. */
#define TR_TG_R_WAITING_NETWORK                                                                                                                                \
    "Waiting for a route to the Internet. The station has no IP address yet, so no name can be resolved and no TLS session can be opened. Check the Wireless " \
    "page: the bot needs a station connection, an access point on its own is not enough."
/** Diagnosis shown when the host name could not be resolved, rendered on the Telegram page. */
#define TR_TG_R_DNS_FAILED                                                                                                                                     \
    "api.telegram.org could not be resolved. The station has an address but no working name server: check the DNS server handed out by your router, and that " \
    "outbound port 53 is not blocked. The detail row carries how long the lookup ran before giving up."
/** Diagnosis shown when a plain TCP connection could not be opened, rendered on the Telegram page. */
#define TR_TG_R_TCP_FAILED                                                                                                                                     \
    "The name resolved but no TCP connection to port 443 could be opened. The route to the Internet is down, or outbound HTTPS is blocked by a firewall or a " \
    "captive portal. The detail row carries the address tried, the elapsed time and the socket error number."
/** Diagnosis shown when the heap could not satisfy the bring-up, rendered on the Telegram page. */
#define TR_TG_R_NO_MEMORY                                                                                                                                      \
    "Not enough free memory for a TLS session. This is the tightest resource on this board: a session costs tens of kilobytes and the radio modem, the WiFi "  \
    "stack and this web server are already holding theirs. Watch the free heap on the dashboard, and consider turning off a service you do not use."
/** Diagnosis shown when the service refused to initialize, rendered on the Telegram page. */
#define TR_TG_R_INIT_FAILED                                                                                                                                    \
    "The Telegram service refused to initialize. The detail row carries the exact ESP-IDF error together with the free heap and largest free block, so a "     \
    "healthy pair of numbers there rules memory out; a table-full line in the serial log names the fixed-size table that has to be enlarged. It will be "      \
    "retried in a minute."
/** Diagnosis shown when the call to Telegram did not complete, rendered on the Telegram page. */
#define TR_TG_R_CONNECT_FAILED                                                                                                                                 \
    "The station could not complete a call to api.telegram.org. The detail row carries the exact ESP-IDF error. Typical causes are a blocked or filtered "     \
    "Internet connection, a failing name lookup, or a TLS handshake refused because the root certificate does not match the server chain. It will be retried " \
    "in a minute."
/** Diagnosis shown when Telegram answered and refused, rendered on the Telegram page. */
#define TR_TG_R_API_REJECTED                                                                                                                                   \
    "Telegram answered and refused the token. The detail row carries the error code and the wording Telegram itself returned; 401 Unauthorized means the "     \
    "token is wrong or has been revoked, 404 Not Found means the bot no longer exists. Correct the token above; this is not retried on its own."
/** Diagnosis shown when the polling task could not be created, rendered on the Telegram page. */
#define TR_TG_R_TASK_FAILED "The polling task could not be created, which on this board always means the heap could not supply its stack."
/** Diagnosis shown while the bot is connected and polling, rendered on the Telegram page. */
#define TR_TG_R_CONNECTED "Connected to Telegram and polling for updates."

/** @} */

#endif // LANG_EN_H
