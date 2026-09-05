// @file web_help.c
//
// @author Emiliano Augusto Gonzalez ( lu3vea @ gmail . com)
// @date 2026
// @copyright GNU General Public License v3
// @see https://github.com/hiperiondev/esp32idf_APRS
//
// @note
// This is based on other projects:
//     VP-Digi: https://github.com/sq8vps/vp-digi
//     ESP32APRS: https://github.com/nakhonthai/ESP32APRS_Audio
//     LibAPRS: https://github.com/markqvist/LibAPRS
//
//     please contact their authors for more information.
//
// @brief Contextual help registry: the option-label to help-text table, and
// the renderer for the question mark that closes every option label.

#include "web_help.h"

#include <stdio.h>
#include <string.h>

#include "translations.h"
#include "web_common.h"

// The two size bounds in web_help.h are written as plain literals because a
// printf precision cannot be an expression, so they are tied back to
// WEB_HELP_MAX_BYTES here instead: six bytes per source byte is the worst the
// HTML escape can do, and MARKUP_HTML_BYTES below is the fixed markup
// web_help_markup() wraps the escaped text in.
#define MARKUP_HTML_BYTES 189
_Static_assert(WEB_HELP_ESCAPED_MAX == WEB_HELP_MAX_BYTES * 6, "WEB_HELP_ESCAPED_MAX must stay six times WEB_HELP_MAX_BYTES");
_Static_assert(WEB_HELP_MARKUP_MAX > WEB_HELP_ESCAPED_MAX + MARKUP_HTML_BYTES, "WEB_HELP_MARKUP_MAX must hold the escaped text plus the markup around it");

// One row per option label any page can render. The label macro is the key
// because that is what the emitters already receive: a page calls
// web_field_int(req, TR_F_SSID, ...) exactly as it did before this table
// existed, and the help arrives with the label instead of having to be
// threaded through every call site on every page. It also means a label used
// by several pages is explained once, in one place, and reads identically on
// all of them.
//
// Adding an option therefore means adding its TR_H_xxx string to all three
// lang_xx.h files and one row here. Leaving the row out is not a build error -
// the option simply renders with no question mark - so the table is worth a
// glance whenever a page grows a field.
//
// The key is the label's TEXT, not its macro, so two options whose labels read
// the same in some language cannot both be answered from here: the first
// matching row wins. Where that happens and the two mean the same thing - the
// decay ratio and slow repeat rate shared by bulletins and objects - they are
// written to carry one explanation that is true of both. Where they mean
// different things - "Mode", "Name", "Callsign" - neither is listed here at
// all, and the pages that render them pass their TR_H_xxx string straight to
// web_help_markup(). Keeping those out of the table is what stops a lookup
// from quietly answering with the other option's explanation.
static const struct {
    const char *label; // label macro the emitters are called with
    const char *help;  // its help text, bounded by WEB_HELP_MAX_BYTES
} help_table[] = {
    { TR_BM_ENABLE, TR_H_BM_ENABLE },
    { TR_BM_MONITOR, TR_H_BM_MONITOR },
    { TR_BM_MSG_INET_ONLY, TR_H_BM_MSG_INET_ONLY },
    { TR_BM_GATEWAY, TR_H_BM_GATEWAY },
    { TR_F_3RDPARTY_UNWRAP_EN, TR_H_F_3RDPARTY_UNWRAP_EN },
    { TR_F_ADD_TIMESTAMP, TR_H_F_ADD_TIMESTAMP },
    { TR_F_AFSK_MODULATION, TR_H_F_AFSK_MODULATION },
    { TR_F_ALTITUDE_M, TR_H_F_ALTITUDE_M },
    { TR_F_ANTENNA_DIRECTION, TR_H_F_ANTENNA_DIRECTION },
    { TR_F_ANTENNA_GAIN, TR_H_F_ANTENNA_GAIN },
    { TR_F_AUDIO_LOW_PASS_FILTER, TR_H_F_AUDIO_LOW_PASS_FILTER },
    { TR_F_BEACON_INTERVAL_S, TR_H_F_BEACON_INTERVAL_S },
    { TR_F_BEACON_POSITION_2, TR_H_F_BEACON_POSITION_2 },
    { TR_F_BEACON_VIA_INTERNET, TR_H_F_BEACON_VIA_INTERNET },
    { TR_F_BEACON_VIA_RF, TR_H_F_BEACON_VIA_RF },
    { TR_F_BUDLIST_MODE_INET2RF, TR_H_F_BUDLIST_MODE_INET2RF },
    { TR_F_BUDLIST_MODE_RF2INET, TR_H_F_BUDLIST_MODE_RF2INET },
    { TR_F_BULLETIN_DECAY, TR_H_F_BULLETIN_DECAY },
    { TR_F_BULLETIN_EXPIRE, TR_H_F_BULLETIN_EXPIRE },
    { TR_F_BULLETIN_GROUP, TR_H_F_BULLETIN_GROUP },
    { TR_F_BULLETIN_ID, TR_H_F_BULLETIN_ID },
    { TR_F_BULLETIN_MSG, TR_H_F_BULLETIN_MSG },
    { TR_F_BULLETIN_SLOW_RATE, TR_H_F_BULLETIN_SLOW_RATE },
    { TR_F_COMMENT, TR_H_F_COMMENT },
    { TR_F_COMPRESS_POSITION, TR_H_F_COMPRESS_POSITION },
    { TR_F_CSMA_PERSISTENCE, TR_H_F_CSMA_PERSISTENCE },
    { TR_F_DATA_INTERVAL_S, TR_H_F_DATA_INTERVAL_S },
    { TR_F_DIGI_DEST_SSID, TR_H_F_DIGI_DEST_SSID },
    { TR_F_DIGI_FILLIN_ONLY, TR_H_F_DIGI_FILLIN_ONLY },
    { TR_F_DIGI_PREEMPT, TR_H_F_DIGI_PREEMPT },
    { TR_F_DIGI_TRAP_ACTION, TR_H_F_DIGI_TRAP_ACTION },
    { TR_F_DIGI_ALIAS, TR_H_F_DIGI_ALIAS },
    { TR_F_DIGI_MAX_N, TR_H_F_DIGI_MAX_N },
    { TR_F_DUP_CACHE_SIZE, TR_H_F_DUP_CACHE_SIZE },
    { TR_F_DUP_CACHE_TIMEOUT_MS, TR_H_F_DUP_CACHE_TIMEOUT_MS },
    { TR_F_DUTY_CYCLE_EN, TR_H_F_DUTY_CYCLE_EN },
    { TR_F_DUTY_CYCLE_PCT, TR_H_F_DUTY_CYCLE_PCT },
    { TR_F_ENABLE, TR_H_F_ENABLE },
    { TR_F_ENABLE_DIGIPEATER, TR_H_F_ENABLE_DIGIPEATER },
    { TR_F_ENABLE_EXT, TR_H_F_ENABLE_EXT },
    { TR_F_ENABLE_IGATE, TR_H_F_ENABLE_IGATE },
    { TR_F_ENABLE_MESSAGING, TR_H_F_ENABLE_MESSAGING },
    { TR_F_ENABLE_QUERY, TR_H_F_ENABLE_QUERY },
    { TR_F_ENABLE_TRACKER, TR_H_F_ENABLE_TRACKER },
    { TR_F_ENABLE_WX, TR_H_F_ENABLE_WX },
    { TR_F_EXT_DFS_STRENGTH, TR_H_F_EXT_DFS_STRENGTH },
    { TR_F_EXT_DF_BEARING, TR_H_F_EXT_DF_BEARING },
    { TR_F_EXT_DF_NRQ_N, TR_H_F_EXT_DF_NRQ_N },
    { TR_F_EXT_DF_NRQ_Q, TR_H_F_EXT_DF_NRQ_Q },
    { TR_F_EXT_DF_NRQ_R, TR_H_F_EXT_DF_NRQ_R },
    { TR_F_EXT_RANGE_MI, TR_H_F_EXT_RANGE_MI },
    { TR_F_EXT_TYPE, TR_H_F_EXT_TYPE },
    { TR_F_FILTER, TR_H_F_FILTER },
    { TR_F_FIXED_ALTITUDE_M, TR_H_F_FIXED_ALTITUDE_M },
    { TR_F_FIXED_INTERVAL_S, TR_H_F_FIXED_INTERVAL_S },
    { TR_F_FIXED_LATITUDE, TR_H_F_FIXED_LATITUDE },
    { TR_F_FIXED_LONGITUDE, TR_H_F_FIXED_LONGITUDE },
    { TR_F_FX_25_FORWARD_ERROR_CORRECTED_AX_25, TR_H_F_FX_25_FORWARD_ERROR_CORRECTED_AX_25 },
    { TR_F_HEIGHT_M, TR_H_F_HEIGHT_M },
    { TR_F_INCLUDE_ALTITUDE, TR_H_F_INCLUDE_ALTITUDE },
    { TR_F_INTERNET_TO_RF, TR_H_F_INTERNET_TO_RF },
    { TR_F_LATITUDE, TR_H_F_LATITUDE },
    { TR_F_LOG_AFTER_FILTERS, TR_H_F_LOG_AFTER_FILTERS },
    { TR_F_LONGITUDE, TR_H_F_LONGITUDE },
    { TR_F_MESSAGE_ALARM_ENABLE, TR_H_F_MESSAGE_ALARM_ENABLE },
    { TR_F_MESSAGE_ALARM_PIN, TR_H_F_MESSAGE_ALARM_PIN },
    { TR_F_MICE_POSITION, TR_H_F_MICE_POSITION },
    { TR_F_MICE_POSITION_COMMENT, TR_H_F_MICE_POSITION_COMMENT },
    { TR_F_MSG_GATE_EN, TR_H_F_MSG_GATE_EN },
    { TR_F_MSG_LOCAL_WINDOW_S, TR_H_F_MSG_LOCAL_WINDOW_S },
    { TR_F_MSG_MAX_HOPS, TR_H_F_MSG_MAX_HOPS },
    { TR_F_MY_CALLSIGN, TR_H_F_MY_CALLSIGN },
    { TR_F_NO_ARCHIVE, TR_H_F_NO_ARCHIVE },
    { TR_F_OBJECT_ITEM_NAME, TR_H_F_OBJECT_ITEM_NAME },
    { TR_F_OBJECT_NAME, TR_H_F_OBJECT_NAME },
    { TR_F_OBJITEM_ACTIVE, TR_H_F_OBJITEM_ACTIVE },
    { TR_F_OBJITEM_AREA_COLOR, TR_H_F_OBJITEM_AREA_COLOR },
    { TR_F_OBJITEM_AREA_LAT_OFF, TR_H_F_OBJITEM_AREA_LAT_OFF },
    { TR_F_OBJITEM_AREA_LON_OFF, TR_H_F_OBJITEM_AREA_LON_OFF },
    { TR_F_OBJITEM_AREA_SHAPE, TR_H_F_OBJITEM_AREA_SHAPE },
    { TR_F_OBJITEM_AREA_WIDTH, TR_H_F_OBJITEM_AREA_WIDTH },
    { TR_F_OBJITEM_COURSE, TR_H_F_OBJITEM_COURSE },
    { TR_F_OBJITEM_DCS_CODE, TR_H_F_OBJITEM_DCS_CODE },
    { TR_F_OBJITEM_DCS_ENABLE, TR_H_F_OBJITEM_DCS_ENABLE },
    { TR_F_OBJITEM_DECAY, TR_H_F_OBJITEM_DECAY },
    { TR_F_OBJITEM_DUPLEX, TR_H_F_OBJITEM_DUPLEX },
    { TR_F_OBJITEM_FREQ, TR_H_F_OBJITEM_FREQ },
    { TR_F_OBJITEM_INIT_RATE, TR_H_F_OBJITEM_INIT_RATE },
    { TR_F_OBJITEM_NARROW, TR_H_F_OBJITEM_NARROW },
    { TR_F_OBJITEM_OFFSET, TR_H_F_OBJITEM_OFFSET },
    { TR_F_OBJITEM_RANGE, TR_H_F_OBJITEM_RANGE },
    { TR_F_OBJITEM_RANGE_UNIT, TR_H_F_OBJITEM_RANGE_UNIT },
    { TR_F_OBJITEM_RX_FREQ, TR_H_F_OBJITEM_RX_FREQ },
    { TR_F_OBJITEM_RX_FREQ_ENABLE, TR_H_F_OBJITEM_RX_FREQ_ENABLE },
    { TR_F_OBJITEM_SCOPE, TR_H_F_OBJITEM_SCOPE },
    { TR_F_OBJITEM_SIGNPOST, TR_H_F_OBJITEM_SIGNPOST },
    { TR_F_OBJITEM_SLOW_RATE, TR_H_F_OBJITEM_SLOW_RATE },
    { TR_F_OBJITEM_SPEED, TR_H_F_OBJITEM_SPEED },
    { TR_F_OBJITEM_SYMBOL, TR_H_F_OBJITEM_SYMBOL },
    { TR_F_OBJITEM_TONE, TR_H_F_OBJITEM_TONE },
    { TR_F_OBJITEM_TYPE, TR_H_F_OBJITEM_TYPE },
    { TR_F_OBJITEM_PATH_FMT, TR_H_F_OBJITEM_PATH_FMT },
    { TR_F_PARM_UNIT_EQNS_INTERVAL_S, TR_H_F_PARM_UNIT_EQNS_INTERVAL_S },
    { TR_F_PATH, TR_H_F_PATH },
    { TR_F_POS_AMBIGUITY, TR_H_F_POS_AMBIGUITY },
    { TR_F_POS_DAO, TR_H_F_POS_DAO },
    { TR_F_PREAMBLE_MS, TR_H_F_PREAMBLE_MS },
    { TR_F_PREFIXES, TR_H_F_PREFIXES },
    { TR_F_PREFIX_FILTER_EN, TR_H_F_PREFIX_FILTER_EN },
    { TR_F_PTT_MIN_UNKEY_MS, TR_H_F_PTT_MIN_UNKEY_MS },
    { TR_F_QUERY_APRS, TR_H_F_QUERY_APRS },
    { TR_F_QUERY_CAP_ENABLE, TR_H_F_QUERY_CAP_ENABLE },
    { TR_F_QUERY_CAP_EXTRA, TR_H_F_QUERY_CAP_EXTRA },
    { TR_F_QUERY_CAP_INTERVAL, TR_H_F_QUERY_CAP_INTERVAL },
    { TR_F_QUERY_DIRECTED, TR_H_F_QUERY_DIRECTED },
    { TR_F_QUERY_EXT, TR_H_F_QUERY_EXT },
    { TR_F_QUERY_IGATE, TR_H_F_QUERY_IGATE },
    { TR_F_QUERY_INET, TR_H_F_QUERY_INET },
    { TR_F_QUERY_MIN_INTERVAL, TR_H_F_QUERY_MIN_INTERVAL },
    { TR_F_QUERY_RF, TR_H_F_QUERY_RF },
    { TR_F_QUERY_WX, TR_H_F_QUERY_WX },
    { TR_F_RADIO_TX_POWER, TR_H_F_RADIO_TX_POWER },
    { TR_F_RANGE_FILTER_EN, TR_H_F_RANGE_FILTER_EN },
    { TR_F_RANGE_KM, TR_H_F_RANGE_KM },
    { TR_F_RETRY_COUNT, TR_H_F_RETRY_COUNT },
    { TR_F_RETRY_INTERVAL_S, TR_H_F_RETRY_INTERVAL_S },
    { TR_F_RF_TO_INTERNET, TR_H_F_RF_TO_INTERNET },
    { TR_F_RF_TX_BUFFERS, TR_H_F_RF_TX_BUFFERS },
    { TR_F_SEND_RECEIVE_VIA_INTERNET, TR_H_F_SEND_RECEIVE_VIA_INTERNET },
    { TR_F_SEND_RECEIVE_VIA_RF, TR_H_F_SEND_RECEIVE_VIA_RF },
    { TR_F_SEND_VIA_INTERNET, TR_H_F_SEND_VIA_INTERNET },
    { TR_F_SEND_VIA_RF, TR_H_F_SEND_VIA_RF },
    { TR_F_SERVER_HOST, TR_H_F_SERVER_HOST },
    { TR_F_SERVER_PORT, TR_H_F_SERVER_PORT },
    { TR_F_SMARTBEACONING_ENABLE, TR_H_F_SMARTBEACONING_ENABLE },
    { TR_F_SMARTBEACONING_FAST_INTERVAL_S, TR_H_F_SMARTBEACONING_FAST_INTERVAL_S },
    { TR_F_SMARTBEACONING_HIGH_SPEED_KMH, TR_H_F_SMARTBEACONING_HIGH_SPEED_KMH },
    { TR_F_SMARTBEACONING_LOW_SPEED_KMH, TR_H_F_SMARTBEACONING_LOW_SPEED_KMH },
    { TR_F_SMARTBEACONING_MIN_TURN_TIME_S, TR_H_F_SMARTBEACONING_MIN_TURN_TIME_S },
    { TR_F_SMARTBEACONING_SLOW_INTERVAL_S, TR_H_F_SMARTBEACONING_SLOW_INTERVAL_S },
    { TR_F_SMARTBEACONING_TURN_ANGLE, TR_H_F_SMARTBEACONING_TURN_ANGLE },
    { TR_F_SMARTBEACONING_TURN_SLOPE, TR_H_F_SMARTBEACONING_TURN_SLOPE },
    { TR_F_SSID, TR_H_F_SSID },
    { TR_F_STATION_SYMBOL, TR_H_F_STATION_SYMBOL },
    { TR_F_STATUS_BEAM, TR_H_F_STATUS_BEAM },
    { TR_F_STATUS_ERP, TR_H_F_STATUS_ERP },
    { TR_F_STATUS_GRID, TR_H_F_STATUS_GRID },
    { TR_F_STATUS_INTERVAL_S_0_OFF, TR_H_F_STATUS_INTERVAL_S_0_OFF },
    { TR_F_STATUS_TEXT, TR_H_F_STATUS_TEXT },
    { TR_F_STATUS_TIMESTAMP, TR_H_F_STATUS_TIMESTAMP },
    { TR_F_TIME_STAMP, TR_H_F_TIME_STAMP },
    { TR_F_TRACKER_PHG, TR_H_F_TRACKER_PHG },
    { TR_F_TRACKER_USE_LIVE_GPS, TR_H_F_TRACKER_USE_LIVE_GPS },
    { TR_F_TX_TIME_SLOT_MS, TR_H_F_TX_TIME_SLOT_MS },
    { TR_F_USERNAME, TR_H_F_USERNAME },
    { TR_F_SATGATE_CALL, TR_H_F_SATGATE_CALL },
    { TR_F_MESSAGE_GROUP_FMT, TR_H_F_MESSAGE_GROUP_FMT },
    { TR_FILT_MESSAGE, TR_H_FILT_MESSAGE },
    { TR_FILT_STATUS, TR_H_FILT_STATUS },
    { TR_FILT_TELEMETRY, TR_H_FILT_TELEMETRY },
    { TR_FILT_WEATHER, TR_H_FILT_WEATHER },
    { TR_FILT_OBJECT, TR_H_FILT_OBJECT },
    { TR_FILT_ITEM, TR_H_FILT_ITEM },
    { TR_FILT_BUOY, TR_H_FILT_BUOY },
    { TR_FILT_POSITION, TR_H_FILT_POSITION },
    { TR_FILT_OTHER, TR_H_FILT_OTHER },
    { TR_GPS_ENABLE, TR_H_GPS_ENABLE },
    { TR_SYSINFO_CPU_FREQ_SET, TR_H_SYSINFO_CPU_FREQ_SET },
    { TR_SYS_NTP_HOST, TR_H_SYS_NTP_HOST },
    { TR_SYS_NTP_HOST2, TR_H_SYS_NTP_HOST2 },
    { TR_SYS_NTP_HOST3, TR_H_SYS_NTP_HOST3 },
    { TR_SYS_PATH_1, TR_H_SYS_PATH_1 },
    { TR_SYS_PATH_2, TR_H_SYS_PATH_2 },
    { TR_SYS_PATH_3, TR_H_SYS_PATH_3 },
    { TR_SYS_PATH_4, TR_H_SYS_PATH_4 },
    { TR_SYS_SYNC_NTP, TR_H_SYS_SYNC_NTP },
    { TR_SYS_TIMEZONE, TR_H_SYS_TIMEZONE },
    { TR_TG_ADMIN_ID, TR_H_TG_ADMIN_ID },
    { TR_TG_BULLETIN_WINDOW, TR_H_TG_BULLETIN_WINDOW },
    { TR_TG_ENABLE, TR_H_TG_ENABLE },
    { TR_TG_F_PEER_ID, TR_H_TG_F_PEER_ID },
    { TR_TG_ROUTE_BULLETINS, TR_H_TG_ROUTE_BULLETINS },
    { TR_TG_ROUTE_MESSAGES, TR_H_TG_ROUTE_MESSAGES },
    { TR_TLM_ANALOG_COUNT, TR_H_TLM_ANALOG_COUNT },
    { TR_TLM_ANALOG_FIELD_WIDTH, TR_H_TLM_ANALOG_FIELD_WIDTH },
    { TR_TLM_AUTO_INC_SEQ, TR_H_TLM_AUTO_INC_SEQ },
    { TR_TLM_COEF_A, TR_H_TLM_COEF_A },
    { TR_TLM_COEF_B, TR_H_TLM_COEF_B },
    { TR_TLM_COEF_C, TR_H_TLM_COEF_C },
    { TR_TLM_COMMENT_TLM, TR_H_TLM_COMMENT_TLM },
    { TR_TLM_DECIMALS, TR_H_TLM_DECIMALS },
    { TR_TLM_DESTINATION, TR_H_TLM_DESTINATION },
    { TR_TLM_DIGITAL_COUNT, TR_H_TLM_DIGITAL_COUNT },
    { TR_TLM_ENABLE_TELEMETRY, TR_H_TLM_ENABLE_TELEMETRY },
    { TR_TLM_GEN_BITS, TR_H_TLM_GEN_BITS },
    { TR_TLM_GEN_EQNS, TR_H_TLM_GEN_EQNS },
    { TR_TLM_GEN_PARM, TR_H_TLM_GEN_PARM },
    { TR_TLM_GEN_UNIT, TR_H_TLM_GEN_UNIT },
    { TR_TLM_OMIT_TRAILING, TR_H_TLM_OMIT_TRAILING },
    { TR_TLM_PATH_DIGIS, TR_H_TLM_PATH_DIGIS },
    { TR_TLM_RAW_MAX, TR_H_TLM_RAW_MAX },
    { TR_TLM_RAW_MIN, TR_H_TLM_RAW_MIN },
    { TR_TLM_SOURCE, TR_H_TLM_SOURCE },
    { TR_TLM_TRAIL_COMMENT, TR_H_TLM_TRAIL_COMMENT },
    { TR_TLM_UNIT, TR_H_TLM_UNIT },
    { TR_USE_MY_STATION_DATA, TR_H_USE_MY_STATION_DATA },
    { TR_USE_GPS_DATA, TR_H_USE_GPS_DATA },
    { TR_WIFI_AP_CHANNEL, TR_H_WIFI_AP_CHANNEL },
    { TR_WIFI_AP_SSID, TR_H_WIFI_AP_SSID },
    { TR_WIFI_TX_POWER, TR_H_WIFI_TX_POWER },
    { TR_WL_AUTO_LOGIN, TR_H_WL_AUTO_LOGIN },
    { TR_WL_COMMENT_EN, TR_H_WL_COMMENT_EN },
    { TR_WL_ENABLE, TR_H_WL_ENABLE },
    { TR_WL_GATE_EXEMPT, TR_H_WL_GATE_EXEMPT },
    { TR_WL_INET_ONLY, TR_H_WL_INET_ONLY },
    { TR_WL_MYCALL, TR_H_WL_MYCALL },
    { TR_WL_POLL_MIN, TR_H_WL_POLL_MIN },
    { TR_WL_SERVICE_CALL, TR_H_WL_SERVICE_CALL },
    { TR_WL_SESSION_MAX_MIN, TR_H_WL_SESSION_MAX_MIN },
    { TR_WL_USE_MSG_CALL, TR_H_WL_USE_MSG_CALL },
};

const char *web_help_for_label(const char *label) {
    if (!label || !label[0])
        return NULL;
    for (size_t i = 0; i < sizeof(help_table) / sizeof(help_table[0]); i++) {
        if (strcmp(help_table[i].label, label) == 0)
            return help_table[i].help;
    }
    return NULL;
}

// Keeps at most WEB_HELP_MAX_BYTES of `src`, backing up over any trailing
// UTF-8 continuation bytes (0b10xxxxxx) and then over their lead byte so the
// copy never ends mid-sequence. The translation tables are UTF-8, so cutting
// at a fixed byte count without this could leave a dangling byte that renders
// as a replacement glyph.
static void help_clamp(char dst[WEB_HELP_MAX_BYTES + 1], const char *src) {
    size_t n = strlen(src);
    if (n > WEB_HELP_MAX_BYTES) {
        n = WEB_HELP_MAX_BYTES;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80)
            n--;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

void web_help_markup(char *dst, size_t dst_size, const char *help) {
    if (!dst || dst_size == 0)
        return;
    dst[0] = 0;
    if (!help || !help[0])
        return;

    char clamped[WEB_HELP_MAX_BYTES + 1];
    help_clamp(clamped, help);

    // Escaped even though the text is a compile-time translation string: the
    // escape is what lets a help text contain an ampersand, a quote or an
    // angle bracket at all, which several of them need to name an APRS field
    // or a comparison.
    char esc[WEB_HELP_ESCAPED_MAX + 1];
    web_html_attr_escape(clamped, esc, sizeof(esc));

    // tabindex makes the marker reachable by keyboard and, on a touch screen,
    // tappable - both of which the stylesheet turns into the same :focus rule
    // that hover uses, so the balloon is not mouse-only. The click handler
    // stops a tap on the marker from also activating the label it sits in,
    // which would otherwise toggle the checkbox the operator was only asking
    // about. aria-hidden on the glyph keeps a screen reader from announcing a
    // bare question mark ahead of the text it stands for.
    //
    // Assembled in a local of known size first, so the compiler can see that
    // no part of the markup is ever truncated, and only then handed to the
    // caller. A destination too small for the whole marker is left holding
    // the empty string rather than half a tag: an option with no question
    // mark reads as an option with nothing to explain, while a cut-off span
    // would break the layout of every field after it.
    char out[WEB_HELP_MARKUP_MAX];
    int n = snprintf(out, sizeof(out),
                     "<span class='hlp' tabindex='0' role='note' onclick='event.preventDefault();event.stopPropagation();'>"
                     "<span class='hlp-mark' aria-hidden='true'>?</span>"
                     "<span class='hlp-box'>%." WEB_HELP_STRINGIFY(WEB_HELP_ESCAPED_MAX) "s</span>"
                                                                                         "</span>",
                     esc);
    if (n < 0 || (size_t)n >= dst_size)
        return;
    memcpy(dst, out, (size_t)n + 1);
}
