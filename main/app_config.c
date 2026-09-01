// @file app_config.c
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
// @brief Persistent application configuration: defaults, load/save of
// /storage/config.json on LittleFS (via cJSON) and the global g_config instance
// shared by every component and web admin page.

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h> // strtod() - shortest round-tripping precision for the JSON number writer
#include <string.h>

#include "app_config.h"
#include "aprs_coord.h" // aprs_symbol_table_is_valid()/aprs_symbol_code_is_valid(): the symbol pair accepted on air
// RF_TX_BUFFERS_MIN/MAX, RF_PREAMBLE_MS_MIN/MAX, RF_TX_TIMESLOT_MS_MAX,
// PTT_MIN_UNKEY_MS_MAX, CSMA_PERSIST_MIN: the same bounds the Radiomodem form
// enforces, so what is loaded from flash and what is saved from the web admin
// can never accept different values.
#include "aprs_service.h"
#include "cJSON.h"
// MODEM_PTT_ACTIVE_HIGH: the board's PTT polarity, #ifndef-guarded in that
// header and overridden from the top-level CMakeLists.txt. Both it and
// MODEM_PTT_GPIO (the PTT pin itself) are fixed compile-time constants with
// no g_config counterpart - see aprs_service_build_modem_config().
#include "esp32idf_radioamateur_modem_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "json_escape.h"   // json_write_escaped()
#include "json_store.h"    // shared JSON-file store scaffolding
#include "sensors_local.h" // sensors_local_channel_name() / _from_name() - WX field mappings are stored by driver name, not registry index
#include "storage.h"       // storage_write_lock() - keeps a save from overlapping a whole-partition format
#include "str_append.h"    // str_copy_utf8_safe()
#include "time_sync.h"     // time_sync_tz_count() - bounds g_config.timezone_idx on load

static const char *TAG = "app_config";
#define CONFIG_PATH     "/storage/config.json"
#define CONFIG_TMP_PATH "/storage/config.json.tmp"

app_config_t g_config;

// Serializes every save/load of the underlying config.json file. Without
// this, two overlapping POSTs (e.g. a user clicking Save twice quickly, or a
// page auto-refresh racing a save) could both end up inside
// app_config_save() at once, doing redundant work and each rewriting the
// live config from under the other. (Note: esp_littlefs already takes a
// per-instance lock around every VFS op, so overlapping saves cannot corrupt
// the filesystem's own metadata, and the stdio buffer allocation that a save
// would otherwise make on its first write is handled separately - see
// json_store_open_tmp(). This mutex is here because serializing saves is
// correct on its own terms: it avoids wasted flash writes and it is what lets
// a save reuse a single static stdio buffer.) Created lazily so this file has
// no init-order dependency. The handle is read and written with
// __atomic_load_n()/__atomic_store_n() so every caller, including the one
// outside the critical section, sees either NULL or a fully constructed
// semaphore, never a partially published pointer.
//
// Returns NULL when the heap had no room for the mutex: every caller tests
// the handle before using it, so an out-of-memory device reports a failed save
// instead of taking a NULL handle, which aborts the calling task.
static SemaphoreHandle_t s_config_mutex = NULL;

static SemaphoreHandle_t config_mutex(void) {
    SemaphoreHandle_t m = __atomic_load_n(&s_config_mutex, __ATOMIC_ACQUIRE);
    if (!m) {
        // Allocated before the critical section is entered, so the heap walk
        // never runs with interrupts masked; the critical section only ever
        // publishes the winning handle. A concurrent loser's candidate is
        // freed once outside the lock.
        SemaphoreHandle_t candidate = xSemaphoreCreateMutex();
        static portMUX_TYPE creation_lock = portMUX_INITIALIZER_UNLOCKED;
        taskENTER_CRITICAL(&creation_lock);
        m = s_config_mutex;
        if (!m) {
            m = candidate;
            candidate = NULL;
            __atomic_store_n(&s_config_mutex, m, __ATOMIC_RELEASE);
        }
        taskEXIT_CRITICAL(&creation_lock);
        if (candidate)
            vSemaphoreDelete(candidate);
    }
    return m;
}

// Short-held lock guarding concurrent access to the live g_config struct
// itself. Distinct from s_config_mutex above (which is held across the entire
// flash serialization in app_config_save() and would stall readers): this one
// is a strict LEAF lock, only ever held long enough to mutate/copy a few
// fields. See the app_config_lock() contract in app_config.h. Created lazily
// with the same one-time-init guard as config_mutex(), and NULL under the same
// out-of-memory condition. The handle is read and written with
// __atomic_load_n()/__atomic_store_n() so every caller, including the one
// outside the critical section, sees either NULL or a fully constructed
// semaphore, never a partially published pointer.
static SemaphoreHandle_t s_data_mutex = NULL;

static SemaphoreHandle_t data_mutex(void) {
    SemaphoreHandle_t m = __atomic_load_n(&s_data_mutex, __ATOMIC_ACQUIRE);
    if (!m) {
        // Allocated before the critical section is entered, so the heap walk
        // never runs with interrupts masked; the critical section only ever
        // publishes the winning handle. A concurrent loser's candidate is
        // freed once outside the lock.
        SemaphoreHandle_t candidate = xSemaphoreCreateMutex();
        static portMUX_TYPE creation_lock = portMUX_INITIALIZER_UNLOCKED;
        taskENTER_CRITICAL(&creation_lock);
        m = s_data_mutex;
        if (!m) {
            m = candidate;
            candidate = NULL;
            __atomic_store_n(&s_data_mutex, m, __ATOMIC_RELEASE);
        }
        taskEXIT_CRITICAL(&creation_lock);
        if (candidate)
            vSemaphoreDelete(candidate);
    }
    return m;
}

void app_config_lock(void) {
    // A device that could not allocate the mutex runs unserialized rather than
    // aborting on a NULL handle: the same trade storage.c's writer gate makes,
    // and the only alternative on a heap too small to hold one semaphore.
    SemaphoreHandle_t m = data_mutex();
    if (m)
        xSemaphoreTake(m, portMAX_DELAY);
}

void app_config_unlock(void) {
    SemaphoreHandle_t m = __atomic_load_n(&s_data_mutex, __ATOMIC_ACQUIRE);
    if (m)
        xSemaphoreGive(m);
}

// Every stored string in app_config_t passes through here, so this is the
// one place a CR or LF typed or pasted into any web-admin field - callsign,
// hostname, filter spec, comment, status, path, anything - is stripped
// before it reaches flash. Every consumer of these fields later writes them
// into a line-oriented output (APRS-IS, the AX.25 TNC2 text form, the JSON
// config file itself), and none of those escape an embedded line break, so
// filtering it out at the point of storage closes the path regardless of
// which field carried it or which output it was eventually written to.
static void set_str(char *dst, size_t sz, const char *val) {
    str_copy_strip_line_breaks(val, dst, sz);
}

// Loads a stored free-text field the same way set_str() loads every other
// stored string, except that the cut is walked back to a whole character
// instead of a whole byte. Reserved for the handful of fields that are
// 8-bit-clean and repeated on the air unchanged (comment and status text,
// per aprs.org/aprs12/utf-8.txt) - every other set_str() call is a coded or
// structured field (callsign, hostname, filter spec, ...) that is
// effectively ASCII, where a byte cut and a character cut always land in the
// same place.
//
// CR and LF are stripped first, into a scratch buffer sized to the largest
// field set_str_utf8() serves (COMMENT_SIZE): both are single-byte ASCII,
// never a lead or continuation byte of a multi-byte UTF-8 sequence, so
// removing them ahead of the UTF-8-safe cut cannot itself split a character,
// and doing so here gives this path the same line-break guarantee set_str()
// gives every other stored string. The UTF-8-safe cut then runs from the
// scratch buffer into dst to enforce the byte budget.
static void set_str_utf8(char *dst, size_t sz, const char *val) {
    char stripped[COMMENT_SIZE];
    str_copy_strip_line_breaks(val, stripped, sizeof(stripped) < sz ? sizeof(stripped) : sz);
    str_copy_utf8_safe(stripped, dst, sz);
}

void app_config_set_defaults(app_config_t *c) {
    memset(c, 0, sizeof(*c));

    c->synctime = true;
    c->cpuFreq = 240;
    c->timezone_idx = 0; // UTC

    set_str(c->my_callsign, sizeof(c->my_callsign), "NOCALL");
    // Coordinates start at 0/0 and are transmitted as they stand. APRS has no
    // "position unknown" form, so a station that beacons before the operator
    // sets these puts a real position in the Gulf of Guinea on the air; the
    // placeholder callsign above is what keeps that from being mistaken for a
    // licensed station.
    c->my_lat = 0.0f;
    c->my_lon = 0.0f;
    c->my_alt = 0.0f;

    c->my_phg_power = 1;
    c->my_phg_gain = 6.0f;
    c->my_phg_height = 10;
    c->my_phg_dir = 0;

    c->pos_ambiguity = 0;
    c->status_grid_en = false;
    c->status_timestamp_en = false;
    c->status_beam_deg = STATUS_BEAM_DEG_OFF;
    c->status_erp_watts = 0;
    c->my_no_archive = false;
    c->pos_dao_en = false;

    // SoftAP only: an unconfigured device has no station credentials to use,
    // and the AP is what makes the web admin reachable out of the box.
    c->wifi_mode = WIFI_MODE_CFG_DEFAULT;
    c->wifi_power = WIFI_TX_POWER_DBM_DEFAULT;
    for (int i = 0; i < WIFI_STA_NUM; i++) {
        c->wifi_sta[i].enable = false;
        set_str(c->wifi_sta[i].wifi_ssid, sizeof(c->wifi_sta[i].wifi_ssid), "WIFI_AP");
        set_str(c->wifi_sta[i].wifi_pass, sizeof(c->wifi_sta[i].wifi_pass), "");
    }
    c->wifi_ap_ch = WIFI_AP_CH_DEFAULT;
    set_str(c->wifi_ap_ssid, sizeof(c->wifi_ap_ssid), "esp32idf_APRS");
    set_str(c->wifi_ap_pass, sizeof(c->wifi_ap_pass), "esp32idf_APRS");

    // IGATE
    c->igate_en = true;
    c->rf2inet = true;
    c->inet2rf = false;
    c->igate_loc2rf = false;
    c->igate_loc2inet = true;
    c->aprs_ssid = 10;
    set_str(c->aprs_mycall, sizeof(c->aprs_mycall), "NOCALL");
    set_str(c->aprs_passcode, sizeof(c->aprs_passcode), "-1");
    // Slot 0 keeps the original single-server default; the remaining slots
    // start disabled so an upgraded device connects exactly as before until
    // the operator opts into the extra failover servers.
    c->aprs_server[0].enable = true;
    set_str(c->aprs_server[0].host, sizeof(c->aprs_server[0].host), "aprs.dprns.com");
    c->aprs_server[0].port = APRS_PORT_DEFAULT;
    for (int i = 1; i < APRS_SERVER_NUM; i++) {
        c->aprs_server[i].enable = false;
        set_str(c->aprs_server[i].host, sizeof(c->aprs_server[i].host), "aprs.dprns.com");
        c->aprs_server[i].port = APRS_PORT_DEFAULT;
    }
    set_str(c->aprs_filter, sizeof(c->aprs_filter), "");
    c->igate_log_after_filters = false;
    c->igate_bcn = true;
    c->igate_lat = 0.000f;
    c->igate_lon = 0.000f;
    c->igate_alt = 0;
    c->igate_interval = 30;
    set_str(c->igate_symbol, sizeof(c->igate_symbol), "N&");
    // Preset-slot mask over path[0..3], not a service mask: bit 0 is the
    // only slot that ships with a path string.
    c->igate_path = PATH_PRESET_MASK_DEFAULT;
    set_str(c->igate_comment, sizeof(c->igate_comment), "esp32idf_APRS IGate");
    c->igate_sts_interval = 0;
    c->igate_compress = false;
    c->igate_phg_enable = false;
    c->igate_phg_use_station = false;
    c->igate_phg_power = 1;
    c->igate_phg_gain = 6.0f;
    c->igate_phg_height = 10;
    c->igate_phg_dir = 0;
    c->igate_ext_type = APRS_EXT_PHG;
    c->igate_range_miles = 0;
    c->igate_dfs_strength = 0;
    c->igate_df_bearing = 0;
    // A DF report whose N digit is 0 states that the NRQ triplet carries no
    // meaning, which is the honest reading until the operator enters one.
    c->igate_df_nrq_n = 0;
    c->igate_df_nrq_r = 0;
    c->igate_df_nrq_q = 0;
    c->igate_freq_mhz = 0.0f;
    c->igate_tone_tenths = 0;
    c->igate_duplex = 0;
    c->igate_offset_khz = 0;
    c->rf2inetFilter = IGATE_FILT_MESSAGE | IGATE_FILT_STATUS | IGATE_FILT_TELEMETRY | IGATE_FILT_WEATHER | IGATE_FILT_OBJECT | IGATE_FILT_ITEM |
                       IGATE_FILT_BUOY | IGATE_FILT_POSITION | IGATE_FILT_OTHER;
    c->inet2rfFilter = IGATE_FILT_MESSAGE;
    c->rf2inet_budlist_mode = BUDLIST_OFF;
    c->inet2rf_budlist_mode = BUDLIST_OFF;
    for (int i = 0; i < IGATE_BUDLIST_MAX; i++)
        set_str(c->budlist[i], sizeof(c->budlist[i]), "");

    // Satellite/ISS digipeater gate-call list: the same 6 calls this firmware
    // always used before this list became web-configurable. Remaining slots
    // (if IGATE_SATGATE_MAX ever grows past 6) default to empty/unused.
    {
        static const char *satGateDefaults[] = { "RS0ISS", "YBOX", "YBSAT", "PSAT", "W3ADO", "BJ1SI" };
        for (int i = 0; i < IGATE_SATGATE_MAX; i++)
            set_str(c->satgate[i], sizeof(c->satgate[i]), (i < (int)(sizeof(satGateDefaults) / sizeof(satGateDefaults[0]))) ? satGateDefaults[i] : "");
    }
    c->dup_cache_size = DUP_CACHE_SIZE_DEFAULT;
    c->dup_cache_timeout_ms = DUP_CACHE_TIMEOUT_MS_DEFAULT;

    c->rf2inet_range_en = false;
    c->rf2inet_range_km = 0.0f;
    c->rf2inet_prefix_en = false;
    set_str(c->rf2inet_prefixes, sizeof(c->rf2inet_prefixes), "");
    c->inet2rf_range_en = false;
    c->inet2rf_range_km = 0.0f;

    // BrandMeister interconnect: off, and with the worldwide monitor
    // subscription off inside it. Both are opt-in because the feature changes
    // what the station puts on the air, and the gateway list starts empty
    // because the two tests that matter (TOCALL and the DMR alias) need no
    // configuration at all - see aprs_bm.h.
    c->bm_en = false;
    c->bm_monitor = false;
    c->bm_msg_inet_only = true;
    for (int i = 0; i < APRS_BM_GATEWAYS_MAX; i++)
        set_str(c->bm_gateways[i], sizeof(c->bm_gateways[i]), "");
    c->inet2rf_3rdparty_unwrap_en = false;
    c->igate_msg_gate_en = true;
    c->igate_local_window_sec = IGATE_LOCAL_WINDOW_SEC_DEFAULT;
    // Message gating reaches exactly as far as the IGate transmits: an addressee
    // whose frames arrive over a longer path than this station can send back is
    // out of reach whatever the last-heard window says. PATH_PRESET_MASK_DEFAULT
    // above selects preset slot 0 alone, and that slot is filled with
    // PATH_PRESET_DEFAULT, so the reach of the factory transmit path is the hop
    // count of that one string.
    c->igate_msg_max_hops = app_config_path_preset_hops(PATH_PRESET_DEFAULT);
    if (c->igate_msg_max_hops > IGATE_MSG_MAX_HOPS_MAX)
        c->igate_msg_max_hops = IGATE_MSG_MAX_HOPS_MAX;

    // DIGI
    c->digi_en = false;
    c->digi_ssid = 1;
    set_str(c->digi_mycall, sizeof(c->digi_mycall), "NOCALL");
    c->digi_path = PATH_PRESET_MASK_DEFAULT; // preset-slot mask over path[0..3], not a service mask
    c->digi_bcn = true;
    c->digi_compress = false;
    c->digi_interval = 30;
    set_str(c->digi_symbol, sizeof(c->digi_symbol), "N&");
    set_str(c->digi_comment, sizeof(c->digi_comment), "esp32idf_APRS Digi");
    c->digi_phg_enable = false;
    c->digi_phg_use_station = false;
    c->digi_phg_power = 1;
    c->digi_phg_gain = 6.0f;
    c->digi_phg_height = 10;
    c->digi_phg_dir = 0;
    c->digi_ext_type = APRS_EXT_PHG;
    c->digi_range_miles = 0;
    c->digi_dfs_strength = 0;
    c->digi_df_bearing = 0;
    // A DF report whose N digit is 0 states that the NRQ triplet carries no
    // meaning, which is the honest reading until the operator enters one.
    c->digi_df_nrq_n = 0;
    c->digi_df_nrq_r = 0;
    c->digi_df_nrq_q = 0;
    c->digi_freq_mhz = 0.0f;
    c->digi_tone_tenths = 0;
    c->digi_duplex = 0;
    c->digi_offset_khz = 0;

    // Factory alias table: the New n-N Paradigm's two standard aliases get a
    // row each so their hop limits can differ, and a wildcard row catches the
    // rest of the WIDEn family and traps it down to two hops. Every row traces
    // (inserts this station's callsign), which is what makes each hop of a
    // repeated path identifiable. The fourth row is free for a regional
    // SSn-N alias.
    {
        static const digi_alias_t aliasDefaults[] = {
            { "WIDE1", 1, DIGI_ALIAS_TRACE },
            { "WIDE2", 2, DIGI_ALIAS_TRACE },
            { "WIDE#", 2, DIGI_ALIAS_TRACE },
            { "", 1, DIGI_ALIAS_OFF },
        };
        for (int i = 0; i < DIGI_ALIAS_MAX; i++) {
            if (i < (int)(sizeof(aliasDefaults) / sizeof(aliasDefaults[0]))) {
                c->digi_alias[i] = aliasDefaults[i];
            } else {
                set_str(c->digi_alias[i].alias, sizeof(c->digi_alias[i].alias), "");
                c->digi_alias[i].max_n = 1;
                c->digi_alias[i].mode = DIGI_ALIAS_OFF;
            }
        }
    }
    c->digi_fillin_only = false;
    c->digi_trap_n_clamp = true;
    // Routing on the destination SSID alone is pre-New-N behaviour that
    // bypasses the alias table entirely, so it stays off until an operator who
    // still has a legacy neighbour asks for it.
    c->digi_dest_ssid_en = false;
    // Scanning past the first unused address changes which stations this
    // digipeater answers for, so it is opt-in: a station that has not been
    // asked to serve an explicit route behaves exactly as the alias table says.
    c->digi_preempt = DIGI_PREEMPT_OFF;

    // TRACKER
    c->trk_en = false;
    c->trk_ssid = 9;
    set_str(c->trk_mycall, sizeof(c->trk_mycall), "NOCALL");
    c->trk_path = PATH_PRESET_MASK_DEFAULT; // preset-slot mask over path[0..3], not a service mask
    c->trk_interval = 60;
    c->trk_compress = false;
    c->trk_phg_enable = false;
    c->trk_mice = false;
    c->trk_mice_msg = MICE_POS_COMMENT_DEFAULT;
    set_str(c->trk_symbol, sizeof(c->trk_symbol), "\\>");
    set_str(c->trk_comment, sizeof(c->trk_comment), "esp32idf_APRS Tracker");
    c->trk_freq_mhz = 0.0f;
    c->trk_tone_tenths = 0;
    c->trk_duplex = 0;
    c->trk_offset_khz = 0;

    // SmartBeaconing (Hans-Gunnar Lundahl / HamHUD algorithm), off by
    // default: an operator has to opt in and also enable "Use live GPS fix",
    // since neither speed nor course exists without a live fix.
    c->trk_sb_enable = false;
    c->trk_sb_slow_interval = TRK_SB_SLOW_INTERVAL_S_DEFAULT;
    c->trk_sb_fast_interval = TRK_SB_FAST_INTERVAL_S_DEFAULT;
    c->trk_sb_low_speed_kmh = TRK_SB_LOW_SPEED_KMH_DEFAULT;
    c->trk_sb_high_speed_kmh = TRK_SB_HIGH_SPEED_KMH_DEFAULT;
    c->trk_sb_turn_angle = TRK_SB_TURN_ANGLE_DEFAULT;
    c->trk_sb_turn_slope = TRK_SB_TURN_SLOPE_DEFAULT;
    c->trk_sb_min_turn_time = TRK_SB_MIN_TURN_TIME_S_DEFAULT;

    // GNSS receiver
    c->gps_en = false;

    // WX
    c->wx_en = false;
    c->wx_ssid = 13;
    set_str(c->wx_mycall, sizeof(c->wx_mycall), "NOCALL");
    c->wx_path = PATH_PRESET_MASK_DEFAULT; // preset-slot mask over path[0..3], not a service mask
    c->wx_interval = 300;
    set_str(c->wx_comment, sizeof(c->wx_comment), APRS_SOFTWARE_NAME " WX");
    // Enable the WX fields a typical station reports; the rest stay off until
    // the operator maps a sensor channel to them on the Weather page.
    //
    // wx_sensor_ch[] defaults to SENSOR_LOCAL_CH_NONE ("(none)" - see page_wx.c's
    // wx_channel_select()/wx_field_present() and weather.c's
    // weather_refresh_now(), which both treat it as "no source channel
    // picked"). Channel 0 is a REAL registry slot (the first registered
    // driver), not a placeholder, so defaulting to 0 here would wire the
    // fields below straight to whatever driver happened to enumerate first -
    // transmitting live sensor data on first boot even though the operator
    // never mapped a channel in Sensor Mapping. Enable/Averaged/Channel must
    // all be explicitly set by the operator before a field is ever sampled or
    // put on-air; only Channel needs the non-zero default to make that true,
    // since weather_refresh_now() already skips any field whose channel is
    // SENSOR_LOCAL_CH_NONE regardless of its Enable bit.
    for (int i = 0; i < WX_SENSOR_NUM; i++) {
        c->wx_sensor_enable[i] = false;
        c->wx_sensor_avg[i] = false;
        c->wx_sensor_ch[i] = SENSOR_LOCAL_CH_NONE;
    }
    c->wx_sensor_enable[WX_FIELD_WIND_DIRECTION] = true;
    c->wx_sensor_enable[WX_FIELD_WIND_SPEED] = true;
    c->wx_sensor_enable[WX_FIELD_WIND_GUST] = true;
    c->wx_sensor_enable[WX_FIELD_TEMPERATURE] = true;
    c->wx_sensor_enable[WX_FIELD_HUMIDITY] = true;
    c->wx_sensor_enable[WX_FIELD_PRESSURE] = true;

    // Telemetry defaults live in telemetry_config_set_defaults()
    // (telemetry.c); telemetry configuration is not part of g_config.

    // AFSK / TNC
    c->audio_modem_en = true;
    c->audio_lpf = true;
    c->preamble = 300;
    c->afsk_modem_type = 1; // default 1200 Bd (AFSK/Bell202) - standard APRS audio modem
    c->fx25_mode = 0;
    c->tx_timeslot = 2000;
    c->csma_persist = 63;     // ~25% transmit chance per clear slot, the standard AX.25/KISS Persist default
    c->rf_tx_buffers = 1;     // see RF_TX_BUFFERS_MIN/MAX in aprs_service.h
    c->duty_cycle_en = false; // long-term duty-cycle limiter off by default (opt-in) - see DUTY_CYCLE_PCT_MIN/MAX in aprs_service.h
    c->duty_cycle_pct = 25;   // ceiling once enabled: 25% of the rolling window aprs_service.c measures it over
    c->ptt_min_unkey_ms = 0;  // see PTT_MIN_UNKEY_MS_MIN/MAX in aprs_service.h
    set_str(c->ntp_host[0], sizeof(c->ntp_host[0]), "pool.ntp.org");
    set_str(c->ntp_host[1], sizeof(c->ntp_host[1]), "time.google.com");
    set_str(c->ntp_host[2], sizeof(c->ntp_host[2]), "time.cloudflare.com");
    c->ntp_resync_sec = 3600;

    // System / HTTP auth  (README documented default: admin/admin)
    set_str(c->http_username, sizeof(c->http_username), "admin");
    set_str(c->http_password, sizeof(c->http_password), "admin");
    // Shared path presets. Slot 0 carries the generic New n-N Paradigm path and
    // is the slot every beacon selects out of the box (PATH_PRESET_MASK_DEFAULT);
    // the other three are free for the operator's regional aliases.
    for (int i = 0; i < 4; i++)
        set_str(c->path[i], sizeof(c->path[i]), "");
    set_str(c->path[0], sizeof(c->path[0]), PATH_PRESET_DEFAULT);

    // Audio modem PTT.
    //
    // The modem component takes its ADC/DAC pins, PTT pin, PTT active level,
    // ADC attenuation and LED pins as compile-time constants (MODEM_* macros,
    // set in the top-level CMakeLists.txt). None of them are g_config fields
    // any more - see aprs_service_build_modem_config().

    // Message
    c->msg_enable = true;
    set_str(c->msg_mycall, sizeof(c->msg_mycall), "NOCALL");
    c->msg_path = 9;
    c->msg_rf = true;
    c->msg_inet = true;
    c->msg_retry = 3;
    c->msg_interval = 30;
    c->msg_alarm_enable = false; // disabled by default
    c->msg_alarm_gpio = -1;
    for (int i = 0; i < 3; i++)
        c->msg_group[i][0] = 0; // no operator-defined groups by default

    // Query responder
    c->query_en = false; // opt-in, like msg_enable
    c->query_rf = true;
    c->query_inet = false; // a question read off the APRS-IS feed is ignored, so it can never key the transmitter
    c->query_aprs_en = true;
    c->query_wx_en = true;
    c->query_igate_en = true;
    c->query_directed_en = true;
    c->query_ext_en = true;
    c->query_min_interval_sec = 30;
    // Opt-in: a channel full of gateways all announcing themselves is exactly
    // what the reply-only default avoids.
    c->query_cap_beacon_en = false;
    c->query_cap_interval_sec = QUERY_CAP_INTERVAL_S_DEFAULT;
    c->query_cap_rf = true;
    c->query_cap_inet = false;
    c->query_cap_extra[0] = 0;
}

// ---- streaming JSON writer -----------------------------------------------
// The configuration is serialized by writing tokens straight to the open
// config file, one field at a time. Building a cJSON tree of the whole config
// in RAM and printing that tree into a second full-size string buffer would
// cost hundreds of tiny cJSON nodes (~40+ KB) plus a ~7 KB contiguous print
// buffer, all live at once - on this device's small, fragmentable heap the
// single largest memory event in the firmware, enough to drive the "minimum
// free heap" watermark down to a few KB on every save. Streaming keeps the
// extra RAM a save needs to essentially just littlefs's own write buffer.
//
// The schema has only one object level and single-level (scalar) arrays, so a
// single "need a comma before the next item" flag for each context is enough.
typedef struct {
    FILE *f;
    bool obj_comma; // a member has already been written at object level
    bool arr_comma; // an element has already been written in the current array
} jw_t;

// Emit a number: integers without a decimal point, non-integers at the
// shortest precision that still round-trips (mirrors cJSON's number printer
// closely enough that reload via cJSON_Parse yields the same double).
static void jw_num_val(jw_t *w, double v) {
    if (!isfinite(v)) {
        fputs("0", w->f);
        return;
    }
    if (floor(v) == v && fabs(v) < 1e15) {
        fprintf(w->f, "%.0f", v);
        return;
    }
    char tmp[32];
    for (int prec = 7; prec <= 17; prec++) {
        snprintf(tmp, sizeof(tmp), "%.*g", prec, v);
        if (strtod(tmp, NULL) == v)
            break;
    }
    fputs(tmp, w->f);
}

static void jw_key(jw_t *w, const char *k) {
    if (w->obj_comma)
        fputc(',', w->f);
    w->obj_comma = true;
    json_write_escaped(w->f, k);
    fputc(':', w->f);
}

// Object members. The jadd_*(d, "key", value) call signature is what the
// hundreds of call sites below are written against.
static void jadd_str(jw_t *o, const char *k, const char *v) {
    jw_key(o, k);
    json_write_escaped(o->f, v ? v : "");
}
static void jadd_num(jw_t *o, const char *k, double v) {
    jw_key(o, k);
    jw_num_val(o, v);
}
static void jadd_bool(jw_t *o, const char *k, bool v) {
    jw_key(o, k);
    fputs(v ? "true" : "false", o->f);
}

// Scalar arrays: every array written by config_write_json() holds strings,
// numbers or booleans, so those are the three element writers this needs.
static void jarr_begin(jw_t *o, const char *k) {
    jw_key(o, k);
    fputc('[', o->f);
    o->arr_comma = false;
}
static void jarr_end(jw_t *o) {
    fputc(']', o->f);
}
static void jarr_str(jw_t *o, const char *v) {
    if (o->arr_comma)
        fputc(',', o->f);
    o->arr_comma = true;
    json_write_escaped(o->f, v ? v : "");
}
static void jarr_num(jw_t *o, double v) {
    if (o->arr_comma)
        fputc(',', o->f);
    o->arr_comma = true;
    jw_num_val(o, v);
}
static void jarr_bool(jw_t *o, bool v) {
    if (o->arr_comma)
        fputc(',', o->f);
    o->arr_comma = true;
    fputs(v ? "true" : "false", o->f);
}

static const char *jget_str(cJSON *o, const char *k, const char *def) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (v && cJSON_IsString(v))
        return v->valuestring;
    return def;
}
static double jget_num(cJSON *o, const char *k, double def) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (v && cJSON_IsNumber(v))
        return v->valuedouble;
    return def;
}
static bool jget_bool(cJSON *o, const char *k, bool def) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (v && cJSON_IsBool(v))
        return cJSON_IsTrue(v);
    return def;
}

// ---- serialize ------------------------------------------------------------
static void config_write_json(jw_t *d, const app_config_t *c) {
    fputc('{', d->f);
    jadd_num(d, "cpuFreq", c->cpuFreq);
    jadd_str(d, "myCallsign", c->my_callsign);
    jadd_bool(d, "myUseGps", c->my_use_gps);
    jadd_num(d, "myLAT", c->my_lat);
    jadd_num(d, "myLON", c->my_lon);
    jadd_num(d, "myALT", c->my_alt);
    jadd_num(d, "myPHGPower", c->my_phg_power);
    jadd_num(d, "myPHGGain", c->my_phg_gain);
    jadd_num(d, "myPHGHeight", c->my_phg_height);
    jadd_num(d, "myPHGDir", c->my_phg_dir);
    jadd_num(d, "myAmbiguity", c->pos_ambiguity);
    jadd_bool(d, "myStatusGrid", c->status_grid_en);
    jadd_bool(d, "myStatusTS", c->status_timestamp_en);
    jadd_num(d, "myStatusBeam", c->status_beam_deg);
    jadd_num(d, "myStatusERP", c->status_erp_watts);
    jadd_bool(d, "myNoArchive", c->my_no_archive);
    jadd_bool(d, "myPosDao", c->pos_dao_en);
    jadd_num(d, "txTimeSlot", c->tx_timeslot);
    jadd_num(d, "csmaPersist", c->csma_persist);
    jadd_bool(d, "syncTime", c->synctime);
    jadd_str(d, "ntpHost0", c->ntp_host[0]);
    jadd_str(d, "ntpHost1", c->ntp_host[1]);
    jadd_str(d, "ntpHost2", c->ntp_host[2]);
    jadd_num(d, "ntpResync", c->ntp_resync_sec);
    jadd_num(d, "timeZone", c->timezone_idx);
    jadd_num(d, "WiFiMode", c->wifi_mode);
    jadd_num(d, "WiFiPwr", c->wifi_power);
    jadd_num(d, "WiFiAPCH", c->wifi_ap_ch);
    jadd_str(d, "WiFiAP_SSID", c->wifi_ap_ssid);
    jadd_str(d, "WiFiAP_PASS", c->wifi_ap_pass);
    jarr_begin(d, "WiFiSTA");
    for (int i = 0; i < WIFI_STA_NUM; i++) {
        jarr_bool(d, c->wifi_sta[i].enable);
        jarr_str(d, c->wifi_sta[i].wifi_ssid);
        jarr_str(d, c->wifi_sta[i].wifi_pass);
    }
    jarr_end(d);

    jadd_num(d, "fx25Mode", c->fx25_mode);
    jadd_num(d, "afskModem", c->afsk_modem_type);
    jadd_num(d, "rfPreamble", c->preamble);
    jadd_bool(d, "audioModemEn", c->audio_modem_en);
    jadd_bool(d, "audioLPF", c->audio_lpf);
    jadd_num(d, "rfTxBuffers", c->rf_tx_buffers);
    jadd_bool(d, "dutyCycleEn", c->duty_cycle_en);
    jadd_num(d, "dutyCyclePct", c->duty_cycle_pct);
    jadd_num(d, "pttMinUnkeyMs", c->ptt_min_unkey_ms);

    jadd_bool(d, "igateEn", c->igate_en);
    jadd_bool(d, "igateBcn", c->igate_bcn);
    jadd_bool(d, "rf2inet", c->rf2inet);
    jadd_bool(d, "inet2rf", c->inet2rf);
    jadd_bool(d, "igatePos2rf", c->igate_loc2rf);
    jadd_bool(d, "igatePos2inet", c->igate_loc2inet);
    jadd_num(d, "rf2inetFilter", c->rf2inetFilter);
    jadd_num(d, "inet2rfFilter", c->inet2rfFilter);
    jadd_num(d, "rf2inetBudlistMode", c->rf2inet_budlist_mode);
    jadd_num(d, "inet2rfBudlistMode", c->inet2rf_budlist_mode);
    jarr_begin(d, "budlist");
    for (int i = 0; i < IGATE_BUDLIST_MAX; i++)
        jarr_str(d, c->budlist[i]);
    jarr_end(d);
    jarr_begin(d, "satgate");
    for (int i = 0; i < IGATE_SATGATE_MAX; i++)
        jarr_str(d, c->satgate[i]);
    jarr_end(d);
    jadd_num(d, "dupCacheSize", c->dup_cache_size);
    jadd_num(d, "dupCacheTimeoutMs", c->dup_cache_timeout_ms);
    jadd_bool(d, "rf2inetRangeEn", c->rf2inet_range_en);
    jadd_num(d, "rf2inetRangeKm", c->rf2inet_range_km);
    jadd_bool(d, "rf2inetPrefixEn", c->rf2inet_prefix_en);
    jadd_str(d, "rf2inetPrefixes", c->rf2inet_prefixes);
    jadd_bool(d, "inet2rfRangeEn", c->inet2rf_range_en);
    jadd_num(d, "inet2rfRangeKm", c->inet2rf_range_km);
    jadd_bool(d, "bmEn", c->bm_en);
    jadd_bool(d, "bmMonitor", c->bm_monitor);
    jadd_bool(d, "bmMsgInetOnly", c->bm_msg_inet_only);
    jarr_begin(d, "bmGateways");
    for (int i = 0; i < APRS_BM_GATEWAYS_MAX; i++)
        jarr_str(d, c->bm_gateways[i]);
    jarr_end(d);
    jadd_bool(d, "inet2rf3rdPartyUnwrapEn", c->inet2rf_3rdparty_unwrap_en);
    jadd_bool(d, "igateMsgGateEn", c->igate_msg_gate_en);
    jadd_num(d, "igateLocalWindowSec", c->igate_local_window_sec);
    jadd_num(d, "igateMsgMaxHops", c->igate_msg_max_hops);
    jadd_num(d, "igateSSID", c->aprs_ssid);
    jarr_begin(d, "igateServers");
    for (int i = 0; i < APRS_SERVER_NUM; i++) {
        jarr_bool(d, c->aprs_server[i].enable);
        jarr_str(d, c->aprs_server[i].host);
        jarr_num(d, c->aprs_server[i].port);
    }
    jarr_end(d);
    jadd_str(d, "igateMycall", c->aprs_mycall);
    jadd_bool(d, "igateUseStation", c->igate_use_station);
    jadd_bool(d, "igateUseGps", c->igate_use_gps);
    jadd_str(d, "igatePasscode", c->aprs_passcode);
    jadd_str(d, "igateFilter", c->aprs_filter);
    jadd_bool(d, "igateLogAfterFilters", c->igate_log_after_filters);
    jadd_num(d, "igateLAT", c->igate_lat);
    jadd_num(d, "igateLON", c->igate_lon);
    jadd_num(d, "igateALT", c->igate_alt);
    jadd_num(d, "igateINV", c->igate_interval);
    jadd_str(d, "igateSymbol", c->igate_symbol);
    jadd_num(d, "igatePath", c->igate_path);
    jadd_str(d, "igateComment", c->igate_comment);
    jadd_num(d, "igateSTSIntv", c->igate_sts_interval);
    jadd_str(d, "igateStatus", c->igate_status);
    jadd_bool(d, "igateTimestamp", c->igate_timestamp);
    jadd_bool(d, "igateCompress", c->igate_compress);
    jadd_bool(d, "igatePHGEn", c->igate_phg_enable);
    jadd_bool(d, "igatePHGUseStation", c->igate_phg_use_station);
    jadd_num(d, "igatePHGPower", c->igate_phg_power);
    jadd_num(d, "igatePHGGain", c->igate_phg_gain);
    jadd_num(d, "igatePHGHeight", c->igate_phg_height);
    jadd_num(d, "igatePHGDir", c->igate_phg_dir);
    jadd_num(d, "igateExtType", c->igate_ext_type);
    jadd_num(d, "igateRng", c->igate_range_miles);
    jadd_num(d, "igateDfsS", c->igate_dfs_strength);
    jadd_num(d, "igateDfBrg", c->igate_df_bearing);
    jadd_num(d, "igateDfN", c->igate_df_nrq_n);
    jadd_num(d, "igateDfR", c->igate_df_nrq_r);
    jadd_num(d, "igateDfQ", c->igate_df_nrq_q);
    jadd_num(d, "igateFreqMHz", c->igate_freq_mhz);
    jadd_num(d, "igateFreqTone", c->igate_tone_tenths);
    jadd_num(d, "igateFreqDup", c->igate_duplex);
    jadd_num(d, "igateFreqOff", c->igate_offset_khz);

    jadd_bool(d, "digiEn", c->digi_en);
    jadd_bool(d, "digiPos2rf", c->digi_loc2rf);
    jadd_bool(d, "digiPos2inet", c->digi_loc2inet);
    jadd_bool(d, "digiTime", c->digi_timestamp);
    jadd_num(d, "digiSSID", c->digi_ssid);
    jadd_str(d, "digiMycall", c->digi_mycall);
    jadd_bool(d, "digiUseStation", c->digi_use_station);
    jadd_bool(d, "digiUseGps", c->digi_use_gps);
    jadd_num(d, "digiPath", c->digi_path);
    jarr_begin(d, "digiAlias");
    for (int i = 0; i < DIGI_ALIAS_MAX; i++)
        jarr_str(d, c->digi_alias[i].alias);
    jarr_end(d);
    jarr_begin(d, "digiAliasMaxN");
    for (int i = 0; i < DIGI_ALIAS_MAX; i++)
        jarr_num(d, c->digi_alias[i].max_n);
    jarr_end(d);
    jarr_begin(d, "digiAliasMode");
    for (int i = 0; i < DIGI_ALIAS_MAX; i++)
        jarr_num(d, c->digi_alias[i].mode);
    jarr_end(d);
    jadd_bool(d, "digiFillinOnly", c->digi_fillin_only);
    jadd_bool(d, "digiTrapNClamp", c->digi_trap_n_clamp);
    jadd_bool(d, "digiDestSsidEn", c->digi_dest_ssid_en);
    jadd_num(d, "digiPreempt", c->digi_preempt);
    jadd_bool(d, "digiBcn", c->digi_bcn);
    jadd_bool(d, "digiCompress", c->digi_compress);
    jadd_num(d, "digiAlt", c->digi_alt);
    jadd_num(d, "digiLAT", c->digi_lat);
    jadd_num(d, "digiLON", c->digi_lon);
    jadd_num(d, "digiINV", c->digi_interval);
    jadd_str(d, "digiSymbol", c->digi_symbol);
    jadd_str(d, "digiComment", c->digi_comment);
    jadd_num(d, "digiSTSIntv", c->digi_sts_interval);
    jadd_str(d, "digiStatus", c->digi_status);
    jadd_bool(d, "digiPHGEn", c->digi_phg_enable);
    jadd_bool(d, "digiPHGUseStation", c->digi_phg_use_station);
    jadd_num(d, "digiPHGPower", c->digi_phg_power);
    jadd_num(d, "digiPHGGain", c->digi_phg_gain);
    jadd_num(d, "digiPHGHeight", c->digi_phg_height);
    jadd_num(d, "digiPHGDir", c->digi_phg_dir);
    jadd_num(d, "digiExtType", c->digi_ext_type);
    jadd_num(d, "digiRng", c->digi_range_miles);
    jadd_num(d, "digiDfsS", c->digi_dfs_strength);
    jadd_num(d, "digiDfBrg", c->digi_df_bearing);
    jadd_num(d, "digiDfN", c->digi_df_nrq_n);
    jadd_num(d, "digiDfR", c->digi_df_nrq_r);
    jadd_num(d, "digiDfQ", c->digi_df_nrq_q);
    jadd_num(d, "digiFreqMHz", c->digi_freq_mhz);
    jadd_num(d, "digiFreqTone", c->digi_tone_tenths);
    jadd_num(d, "digiFreqDup", c->digi_duplex);
    jadd_num(d, "digiFreqOff", c->digi_offset_khz);

    jadd_bool(d, "trkEn", c->trk_en);
    jadd_bool(d, "trkPos2rf", c->trk_loc2rf);
    jadd_bool(d, "trkPos2inet", c->trk_loc2inet);
    jadd_bool(d, "trkTime", c->trk_timestamp);
    jadd_num(d, "trkSSID", c->trk_ssid);
    jadd_str(d, "trkMycall", c->trk_mycall);
    jadd_bool(d, "trkUseStation", c->trk_use_station);
    jadd_bool(d, "trkUseGps", c->trk_use_gps);
    jadd_bool(d, "trkUseLiveGps", c->trk_use_live_gps);
    jadd_num(d, "trkPath", c->trk_path);
    jadd_num(d, "trkLAT", c->trk_lat);
    jadd_num(d, "trkLON", c->trk_lon);
    jadd_num(d, "trkALT", c->trk_alt);
    jadd_num(d, "trkINV", c->trk_interval);
    jadd_bool(d, "trkCompress", c->trk_compress);
    jadd_bool(d, "trkPHG", c->trk_phg_enable);
    jadd_bool(d, "trkMice", c->trk_mice);
    jadd_num(d, "trkMiceMsg", c->trk_mice_msg);
    jadd_bool(d, "trkOptAlt", c->trk_altitude);
    jadd_str(d, "trkSymbol", c->trk_symbol);
    jadd_str(d, "trkComment", c->trk_comment);
    jadd_num(d, "trkSTSIntv", c->trk_sts_interval);
    jadd_str(d, "trkStatus", c->trk_status);
    jadd_num(d, "trkFreqMHz", c->trk_freq_mhz);
    jadd_num(d, "trkFreqTone", c->trk_tone_tenths);
    jadd_num(d, "trkFreqDup", c->trk_duplex);
    jadd_num(d, "trkFreqOff", c->trk_offset_khz);

    jadd_bool(d, "trkSbEn", c->trk_sb_enable);
    jadd_num(d, "trkSbSlowIntv", c->trk_sb_slow_interval);
    jadd_num(d, "trkSbFastIntv", c->trk_sb_fast_interval);
    jadd_num(d, "trkSbLowSpd", c->trk_sb_low_speed_kmh);
    jadd_num(d, "trkSbHighSpd", c->trk_sb_high_speed_kmh);
    jadd_num(d, "trkSbTurnAngle", c->trk_sb_turn_angle);
    jadd_num(d, "trkSbTurnSlope", c->trk_sb_turn_slope);
    jadd_num(d, "trkSbMinTurnTime", c->trk_sb_min_turn_time);

    jadd_bool(d, "gpsEn", c->gps_en);

    jadd_bool(d, "wxEn", c->wx_en);
    jadd_bool(d, "wxTx2rf", c->wx_2rf);
    jadd_bool(d, "wxTx2inet", c->wx_2inet);
    jadd_bool(d, "wxTime", c->wx_timestamp);
    jadd_num(d, "wxSSID", c->wx_ssid);
    jadd_str(d, "wxMycall", c->wx_mycall);
    jadd_bool(d, "wxUseStation", c->wx_use_station);
    jadd_bool(d, "wxUseGps", c->wx_use_gps);
    jadd_num(d, "wxPath", c->wx_path);
    jadd_num(d, "wxLAT", c->wx_lat);
    jadd_num(d, "wxLON", c->wx_lon);
    jadd_num(d, "wxInv", c->wx_interval);
    jadd_str(d, "wxObject", c->wx_object);
    jadd_str(d, "wxComment", c->wx_comment);
    jarr_begin(d, "wxSenEn");
    for (int i = 0; i < WX_SENSOR_NUM; i++)
        jarr_bool(d, c->wx_sensor_enable[i]);
    jarr_end(d);
    jarr_begin(d, "wxSenAvg");
    for (int i = 0; i < WX_SENSOR_NUM; i++)
        jarr_bool(d, c->wx_sensor_avg[i]);
    jarr_end(d);
    // Sensor mappings travel as driver NAMES, not registry positions: a
    // position only means anything within the firmware image that produced it
    // (see the persistence contract in sensors_local.h), so storing the index
    // would re-point all 13 WX fields at different sensors the moment a driver
    // is enabled or disabled in Kconfig. An unmapped field writes "".
    jarr_begin(d, "wxSenCH");
    for (int i = 0; i < WX_SENSOR_NUM; i++)
        jarr_str(d, sensors_local_channel_name(c->wx_sensor_ch[i]));
    jarr_end(d);

    // Telemetry configuration (channel 0/1, Binary B1-B8 mapping) is no
    // longer part of config.json - see telemetry.h/.c and /storage/telemetry.json.

    jadd_str(d, "httpUser", c->http_username);
    jadd_str(d, "httpPass", c->http_password);
    jarr_begin(d, "path");
    for (int i = 0; i < 4; i++)
        jarr_str(d, c->path[i]);
    jarr_end(d);

    // rfPTT (PTT GPIO) and rfPTTAct (PTT active-high) are not serialized:
    // both are fixed compile-time constants (MODEM_PTT_GPIO /
    // MODEM_PTT_ACTIVE_HIGH), not stored settings.

    jadd_bool(d, "msgEnable", c->msg_enable);
    jadd_str(d, "msgMycall", c->msg_mycall);
    jadd_bool(d, "msgUseStation", c->msg_use_station);
    jadd_bool(d, "msgRf", c->msg_rf);
    jadd_bool(d, "msgInet", c->msg_inet);
    jadd_num(d, "msgPath", c->msg_path);
    jadd_num(d, "msgRetry", c->msg_retry);
    jadd_num(d, "msgInterval", c->msg_interval);
    jadd_bool(d, "msgAlarmEn", c->msg_alarm_enable);
    jadd_num(d, "msgAlarmGpio", c->msg_alarm_gpio);
    jarr_begin(d, "msgGroup");
    for (int i = 0; i < 3; i++)
        jarr_str(d, c->msg_group[i]);
    jarr_end(d);

    jadd_bool(d, "queryEn", c->query_en);
    jadd_bool(d, "queryRf", c->query_rf);
    jadd_bool(d, "queryInet", c->query_inet);
    jadd_bool(d, "queryAprsEn", c->query_aprs_en);
    jadd_bool(d, "queryWxEn", c->query_wx_en);
    jadd_bool(d, "queryIgateEn", c->query_igate_en);
    jadd_bool(d, "queryDirectedEn", c->query_directed_en);
    jadd_bool(d, "queryExtEn", c->query_ext_en);
    jadd_num(d, "queryMinInterval", c->query_min_interval_sec);
    jadd_bool(d, "queryCapEn", c->query_cap_beacon_en);
    jadd_num(d, "queryCapIntv", c->query_cap_interval_sec);
    jadd_bool(d, "queryCapRf", c->query_cap_rf);
    jadd_bool(d, "queryCapInet", c->query_cap_inet);
    jadd_str(d, "queryCapExtra", c->query_cap_extra);

    fputc('}', d->f);
}

// ---- deserialize ------------------------------------------------------------

// Bounds a stored "<table><code>" symbol pair to what chapter 21 defines,
// the same two sets the symbol form enforces. Neither byte is cosmetic: the
// table identifier decides how a receiver reads the rest of a compressed
// position report, and the code decides which classifier the report lands in,
// so a byte that arrived from a hand-edited config.json is folded back to the
// default rather than beaconed.
void app_config_query_cap_extra_sanitize(char *extra) {
    if (extra == NULL)
        return;

    size_t w = 0;
    for (size_t r = 0; extra[r] != 0; r++) {
        char c = extra[r];
        if (c == '\r' || c == '\n' || c == ',' || c == '>')
            continue;
        extra[w++] = c;
    }
    extra[w] = 0;
}

static void clamp_symbol(char *sym, const char *key) {
    if (!aprs_symbol_table_is_valid(sym[0])) {
        ESP_LOGW(TAG, "%s table identifier 0x%02X is not valid, using '%c'", key, (unsigned)(unsigned char)sym[0], APRS_SYMBOL_TABLE_DEFAULT);
        sym[0] = APRS_SYMBOL_TABLE_DEFAULT;
    }
    if (!aprs_symbol_code_is_valid(sym[1])) {
        ESP_LOGW(TAG, "%s code 0x%02X is not valid, using '%c'", key, (unsigned)(unsigned char)sym[1], APRS_SYMBOL_CODE_DEFAULT);
        sym[1] = APRS_SYMBOL_CODE_DEFAULT;
    }
    sym[2] = 0;
}

// Bounds one stored DF report NRQ digit. N, R and Q are a single decimal
// digit each on air, so a wider stored value would push the following bytes
// of the extension out of position for every receiver.
static uint8_t clamp_nrq_digit(double value, const char *key) {
    int v = (int)value;
    if (v < APRS_EXT_DF_NRQ_MIN || v > APRS_EXT_DF_NRQ_MAX) {
        int clamped = (v < APRS_EXT_DF_NRQ_MIN) ? APRS_EXT_DF_NRQ_MIN : APRS_EXT_DF_NRQ_MAX;
        ESP_LOGW(TAG, "%s %d out of range, clamped to %d", key, v, clamped);
        v = clamped;
    }
    return (uint8_t)v;
}

// Bounds one loaded uint16_t field into [min, max], logging and clamping
// a hand-edited or older config.json value the same way clamp_nrq_digit()
// does for a DF digit. Shared by every SmartBeaconing field below, whose
// bounds are all plain uint16_t ranges.
static uint16_t clamp_u16_range(double value, uint16_t min, uint16_t max, const char *key) {
    long v = (long)value;
    if (v < min || v > max) {
        long clamped = (v < min) ? min : max;
        ESP_LOGW(TAG, "%s %ld out of range (%u-%u), clamped to %ld", key, v, (unsigned)min, (unsigned)max, clamped);
        v = clamped;
    }
    return (uint16_t)v;
}

// Bounds one loaded range-gate radius into [APRS_RANGE_KM_MIN,
// APRS_RANGE_KM_MAX]. A NaN reaching the gate would compare false against
// every threshold and quietly turn the gate off, so it is caught here rather
// than at the comparison: the test is written so that a value which is not
// greater than or equal to the floor - NaN included - takes the floor.
static float clamp_range_km(float value, const char *key) {
    if (!(value >= APRS_RANGE_KM_MIN)) {
        ESP_LOGW(TAG, "%s is not a usable distance, clamped to %.0f km", key, (double)APRS_RANGE_KM_MIN);
        return APRS_RANGE_KM_MIN;
    }
    if (value > APRS_RANGE_KM_MAX) {
        ESP_LOGW(TAG, "%s %.1f km out of range, clamped to %.0f km", key, (double)value, (double)APRS_RANGE_KM_MAX);
        return APRS_RANGE_KM_MAX;
    }
    return value;
}

static void config_from_json(cJSON *d, app_config_t *c) {
    // Start from defaults so every key not present in an older config file
    // still ends up with a sane, documented value (never zero-garbage). The
    // defaults are written straight into the destination struct, and every
    // read below takes its fallback from the very field it is about to
    // overwrite: each field is assigned exactly once, always after this call,
    // so at the instant a fallback is read that field still holds its
    // default. Keeping the defaults only in *c is what keeps a second
    // app_config_t - the size of the whole configuration - off the stack of
    // whichever task is loading, on top of the cJSON tree of the whole file
    // that is live in the heap while this runs.
    //
    // A string field's fallback is therefore its own buffer. Both loaders
    // take that: set_str() filters in place, and set_str_utf8() copies
    // through a scratch buffer before touching the field. Either way a
    // default that is already stored gets refiltered to itself.
    app_config_set_defaults(c);

    c->cpuFreq = (uint8_t)jget_num(d, "cpuFreq", c->cpuFreq);
    set_str(c->my_callsign, sizeof(c->my_callsign), jget_str(d, "myCallsign", c->my_callsign));
    c->my_use_gps = jget_bool(d, "myUseGps", c->my_use_gps);
    c->my_lat = (float)jget_num(d, "myLAT", c->my_lat);
    c->my_lon = (float)jget_num(d, "myLON", c->my_lon);
    c->my_alt = (float)jget_num(d, "myALT", c->my_alt);
    c->my_phg_power = (uint16_t)jget_num(d, "myPHGPower", c->my_phg_power);
    c->my_phg_gain = (float)jget_num(d, "myPHGGain", c->my_phg_gain);
    c->my_phg_height = (uint16_t)jget_num(d, "myPHGHeight", c->my_phg_height);
    c->my_phg_dir = (uint8_t)jget_num(d, "myPHGDir", c->my_phg_dir);
    c->pos_ambiguity = (uint8_t)jget_num(d, "myAmbiguity", c->pos_ambiguity);
    if (c->pos_ambiguity > POS_AMBIGUITY_MAX) {
        ESP_LOGW(TAG, "myAmbiguity %u out of range, clamped to %d", (unsigned)c->pos_ambiguity, POS_AMBIGUITY_MAX);
        c->pos_ambiguity = POS_AMBIGUITY_MAX;
    }
    c->status_grid_en = jget_bool(d, "myStatusGrid", c->status_grid_en);
    c->status_timestamp_en = jget_bool(d, "myStatusTS", c->status_timestamp_en);
    {
        // Same two-layer clamp the web form applies, so a hand-edited or
        // imported config.json cannot put a heading or a power on air that the
        // two code characters have no room for. A heading is quantised to the
        // step the field encodes in; anything outside the range switches the
        // block off rather than being folded into an unrelated bearing.
        int beam = (int)jget_num(d, "myStatusBeam", c->status_beam_deg);
        if (beam < 0 || beam > STATUS_BEAM_DEG_MAX) {
            if (beam != STATUS_BEAM_DEG_OFF)
                ESP_LOGW(TAG, "status beam heading %d out of range - beam/ERP block disabled", beam);
            beam = STATUS_BEAM_DEG_OFF;
        } else {
            beam -= beam % STATUS_BEAM_DEG_STEP;
        }
        c->status_beam_deg = (int16_t)beam;

        long erp = (long)jget_num(d, "myStatusERP", c->status_erp_watts);
        if (erp < 0)
            erp = 0;
        if (erp > STATUS_ERP_WATTS_MAX) {
            ESP_LOGW(TAG, "status ERP %ld W above the %d W table maximum - clamped", erp, STATUS_ERP_WATTS_MAX);
            erp = STATUS_ERP_WATTS_MAX;
        }
        c->status_erp_watts = (uint16_t)erp;
    }
    c->my_no_archive = jget_bool(d, "myNoArchive", c->my_no_archive);
    c->pos_dao_en = jget_bool(d, "myPosDao", c->pos_dao_en);
    // Channel-access timing: bound every value coming off flash to the same
    // range the Radiomodem form accepts (aprs_service.h), so a hand-edited or
    // imported config.json cannot hand aprs_service_build_modem_config() a
    // setting the radio should never transmit with - see the note there on
    // what an unbounded preamble does to a shared channel.
    c->tx_timeslot = (uint16_t)jget_num(d, "txTimeSlot", c->tx_timeslot);
    if (c->tx_timeslot > RF_TX_TIMESLOT_MS_MAX) {
        ESP_LOGW(TAG, "txTimeSlot %u out of range, clamped to %d ms", (unsigned)c->tx_timeslot, RF_TX_TIMESLOT_MS_MAX);
        c->tx_timeslot = RF_TX_TIMESLOT_MS_MAX;
    }
    c->csma_persist = (uint8_t)jget_num(d, "csmaPersist", c->csma_persist);
    if (c->csma_persist < CSMA_PERSIST_MIN)
        c->csma_persist = CSMA_PERSIST_MIN;
    c->synctime = jget_bool(d, "syncTime", c->synctime);
    set_str(c->ntp_host[0], sizeof(c->ntp_host[0]), jget_str(d, "ntpHost0", jget_str(d, "ntpHost", c->ntp_host[0])));
    set_str(c->ntp_host[1], sizeof(c->ntp_host[1]), jget_str(d, "ntpHost1", c->ntp_host[1]));
    set_str(c->ntp_host[2], sizeof(c->ntp_host[2]), jget_str(d, "ntpHost2", c->ntp_host[2]));
    c->ntp_resync_sec = (uint16_t)jget_num(d, "ntpResync", c->ntp_resync_sec);
    if (c->ntp_resync_sec < NTP_RESYNC_MIN_SEC)
        c->ntp_resync_sec = NTP_RESYNC_MIN_SEC;
    c->timezone_idx = (uint8_t)jget_num(d, "timeZone", c->timezone_idx);
    if (c->timezone_idx >= time_sync_tz_count()) {
        ESP_LOGW(TAG, "timeZone %u out of range, clamped to 0 (UTC)", (unsigned)c->timezone_idx);
        c->timezone_idx = 0;
    }
    c->wifi_mode = (uint8_t)jget_num(d, "WiFiMode", c->wifi_mode);
    // Read through an int so a value the file carries far outside int8_t range
    // is bounded here rather than wrapping into a small negative on the cast:
    // main.c multiplies this by four for the driver's quarter-dBm argument, so
    // anything outside the accepted band either overflows that multiply or is
    // refused by esp_wifi_set_max_tx_power(), silently leaving whatever power
    // the radio came up with.
    int wifiPwr = (int)jget_num(d, "WiFiPwr", c->wifi_power);
    if (wifiPwr < WIFI_TX_POWER_DBM_MIN || wifiPwr > WIFI_TX_POWER_DBM_MAX) {
        ESP_LOGW(TAG, "stored WiFi TX power %d outside %d-%d dBm, clamped", wifiPwr, WIFI_TX_POWER_DBM_MIN, WIFI_TX_POWER_DBM_MAX);
        wifiPwr = (wifiPwr < WIFI_TX_POWER_DBM_MIN) ? WIFI_TX_POWER_DBM_MIN : WIFI_TX_POWER_DBM_MAX;
    }
    c->wifi_power = (int8_t)wifiPwr;
    c->wifi_ap_ch = (uint8_t)jget_num(d, "WiFiAPCH", c->wifi_ap_ch);
    // The file on flash is not a trusted input: it can arrive from a crafted
    // POST, a hand edit over the Storage page, or a backup taken from a build
    // with a different regulatory range. A channel outside WIFI_AP_CH_MIN..MAX
    // is rejected by esp_wifi_set_config(), which would take the access point
    // - the only way back into the device - down with it, so it is folded back
    // to the default here rather than carried into wifi_init().
    if (c->wifi_ap_ch < WIFI_AP_CH_MIN || c->wifi_ap_ch > WIFI_AP_CH_MAX) {
        ESP_LOGW(TAG, "stored SoftAP channel %u outside %u-%u, using %u", (unsigned)c->wifi_ap_ch, (unsigned)WIFI_AP_CH_MIN, (unsigned)WIFI_AP_CH_MAX,
                 (unsigned)WIFI_AP_CH_DEFAULT);
        c->wifi_ap_ch = WIFI_AP_CH_DEFAULT;
    }
    set_str(c->wifi_ap_ssid, sizeof(c->wifi_ap_ssid), jget_str(d, "WiFiAP_SSID", c->wifi_ap_ssid));
    set_str(c->wifi_ap_pass, sizeof(c->wifi_ap_pass), jget_str(d, "WiFiAP_PASS", c->wifi_ap_pass));
    {
        cJSON *arr = cJSON_GetObjectItemCaseSensitive(d, "WiFiSTA");
        if (arr && cJSON_IsArray(arr)) {
            for (int i = 0; i < WIFI_STA_NUM; i++) {
                cJSON *e = cJSON_GetArrayItem(arr, i * 3);
                cJSON *s = cJSON_GetArrayItem(arr, i * 3 + 1);
                cJSON *p = cJSON_GetArrayItem(arr, i * 3 + 2);
                c->wifi_sta[i].enable = e ? cJSON_IsTrue(e) : false;
                set_str(c->wifi_sta[i].wifi_ssid, sizeof(c->wifi_sta[i].wifi_ssid), (s && cJSON_IsString(s)) ? s->valuestring : c->wifi_sta[i].wifi_ssid);
                set_str(c->wifi_sta[i].wifi_pass, sizeof(c->wifi_sta[i].wifi_pass), (p && cJSON_IsString(p)) ? p->valuestring : c->wifi_sta[i].wifi_pass);
            }
        }
    }

    c->fx25_mode = (uint8_t)jget_num(d, "fx25Mode", c->fx25_mode);
    c->afsk_modem_type = (uint8_t)jget_num(d, "afskModem", c->afsk_modem_type);
    c->preamble = (uint16_t)jget_num(d, "rfPreamble", c->preamble);
    if (c->preamble < RF_PREAMBLE_MS_MIN || c->preamble > RF_PREAMBLE_MS_MAX) {
        ESP_LOGW(TAG, "rfPreamble %u out of range, clamped to %d..%d ms", (unsigned)c->preamble, RF_PREAMBLE_MS_MIN, RF_PREAMBLE_MS_MAX);
        c->preamble = (c->preamble < RF_PREAMBLE_MS_MIN) ? RF_PREAMBLE_MS_MIN : RF_PREAMBLE_MS_MAX;
    }
    c->audio_modem_en = jget_bool(d, "audioModemEn", c->audio_modem_en);
    c->audio_lpf = jget_bool(d, "audioLPF", c->audio_lpf);
    c->rf_tx_buffers = (uint8_t)jget_num(d, "rfTxBuffers", c->rf_tx_buffers);
    if (c->rf_tx_buffers < RF_TX_BUFFERS_MIN)
        c->rf_tx_buffers = RF_TX_BUFFERS_MIN;
    else if (c->rf_tx_buffers > RF_TX_BUFFERS_MAX)
        c->rf_tx_buffers = RF_TX_BUFFERS_MAX;
    c->duty_cycle_en = jget_bool(d, "dutyCycleEn", c->duty_cycle_en);
    c->duty_cycle_pct = (uint8_t)jget_num(d, "dutyCyclePct", c->duty_cycle_pct);
    if (c->duty_cycle_pct < DUTY_CYCLE_PCT_MIN)
        c->duty_cycle_pct = DUTY_CYCLE_PCT_MIN;
    else if (c->duty_cycle_pct > DUTY_CYCLE_PCT_MAX)
        c->duty_cycle_pct = DUTY_CYCLE_PCT_MAX;
    c->ptt_min_unkey_ms = (uint16_t)jget_num(d, "pttMinUnkeyMs", c->ptt_min_unkey_ms);
    if (c->ptt_min_unkey_ms > PTT_MIN_UNKEY_MS_MAX)
        c->ptt_min_unkey_ms = PTT_MIN_UNKEY_MS_MAX;

    c->igate_en = jget_bool(d, "igateEn", c->igate_en);
    c->igate_bcn = jget_bool(d, "igateBcn", c->igate_bcn);
    c->rf2inet = jget_bool(d, "rf2inet", c->rf2inet);
    c->inet2rf = jget_bool(d, "inet2rf", c->inet2rf);
    c->igate_loc2rf = jget_bool(d, "igatePos2rf", c->igate_loc2rf);
    c->igate_loc2inet = jget_bool(d, "igatePos2inet", c->igate_loc2inet);
    c->rf2inetFilter = (uint16_t)jget_num(d, "rf2inetFilter", c->rf2inetFilter);
    // "inet2rfFiltger" was a legacy misspelling of the key used when saving;
    // fall back to it so configs written by older firmware still load correctly.
    c->inet2rfFilter = (uint16_t)jget_num(d, "inet2rfFilter", (double)jget_num(d, "inet2rfFiltger", c->inet2rfFilter));
    c->rf2inet_budlist_mode = (budlist_mode_t)jget_num(d, "rf2inetBudlistMode", c->rf2inet_budlist_mode);
    c->inet2rf_budlist_mode = (budlist_mode_t)jget_num(d, "inet2rfBudlistMode", c->inet2rf_budlist_mode);
    {
        cJSON *bl = cJSON_GetObjectItemCaseSensitive(d, "budlist");
        for (int i = 0; i < IGATE_BUDLIST_MAX; i++) {
            cJSON *v = bl ? cJSON_GetArrayItem(bl, i) : NULL;
            set_str(c->budlist[i], sizeof(c->budlist[i]), (v && cJSON_IsString(v)) ? v->valuestring : c->budlist[i]);
        }
    }
    {
        cJSON *sg = cJSON_GetObjectItemCaseSensitive(d, "satgate");
        for (int i = 0; i < IGATE_SATGATE_MAX; i++) {
            cJSON *v = sg ? cJSON_GetArrayItem(sg, i) : NULL;
            set_str(c->satgate[i], sizeof(c->satgate[i]), (v && cJSON_IsString(v)) ? v->valuestring : c->satgate[i]);
        }
    }
    c->dup_cache_size = (uint8_t)jget_num(d, "dupCacheSize", c->dup_cache_size);
    if (c->dup_cache_size < DUP_CACHE_SIZE_MIN || c->dup_cache_size > DUP_CACHE_SIZE_MAX) {
        ESP_LOGW(TAG, "dupCacheSize %u out of range, clamped to %d..%d", (unsigned)c->dup_cache_size, DUP_CACHE_SIZE_MIN, DUP_CACHE_SIZE_MAX);
        c->dup_cache_size = (c->dup_cache_size < DUP_CACHE_SIZE_MIN) ? DUP_CACHE_SIZE_MIN : DUP_CACHE_SIZE_MAX;
    }
    c->dup_cache_timeout_ms = (uint32_t)jget_num(d, "dupCacheTimeoutMs", c->dup_cache_timeout_ms);
    if (c->dup_cache_timeout_ms < DUP_CACHE_TIMEOUT_MS_MIN || c->dup_cache_timeout_ms > DUP_CACHE_TIMEOUT_MS_MAX) {
        ESP_LOGW(TAG, "dupCacheTimeoutMs %u out of range, clamped to %d..%d ms", (unsigned)c->dup_cache_timeout_ms, DUP_CACHE_TIMEOUT_MS_MIN,
                 DUP_CACHE_TIMEOUT_MS_MAX);
        c->dup_cache_timeout_ms = (c->dup_cache_timeout_ms < DUP_CACHE_TIMEOUT_MS_MIN) ? DUP_CACHE_TIMEOUT_MS_MIN : DUP_CACHE_TIMEOUT_MS_MAX;
    }
    c->rf2inet_range_en = jget_bool(d, "rf2inetRangeEn", c->rf2inet_range_en);
    c->rf2inet_range_km = (float)jget_num(d, "rf2inetRangeKm", c->rf2inet_range_km);
    c->rf2inet_prefix_en = jget_bool(d, "rf2inetPrefixEn", c->rf2inet_prefix_en);
    set_str(c->rf2inet_prefixes, sizeof(c->rf2inet_prefixes), jget_str(d, "rf2inetPrefixes", c->rf2inet_prefixes));

    // Both range gates take the same two-layer clamp the rest of the bounded
    // numerics use: the form emits min/max, and the file on flash is checked
    // again on the way in because it is not a trusted input.
    c->rf2inet_range_km = clamp_range_km(c->rf2inet_range_km, "rf2inetRangeKm");
    c->inet2rf_range_en = jget_bool(d, "inet2rfRangeEn", c->inet2rf_range_en);
    c->inet2rf_range_km = clamp_range_km((float)jget_num(d, "inet2rfRangeKm", c->inet2rf_range_km), "inet2rfRangeKm");

    c->bm_en = jget_bool(d, "bmEn", c->bm_en);
    c->bm_monitor = jget_bool(d, "bmMonitor", c->bm_monitor);
    c->bm_msg_inet_only = jget_bool(d, "bmMsgInetOnly", c->bm_msg_inet_only);
    {
        cJSON *gw = cJSON_GetObjectItemCaseSensitive(d, "bmGateways");
        for (int i = 0; i < APRS_BM_GATEWAYS_MAX; i++) {
            cJSON *v = gw ? cJSON_GetArrayItem(gw, i) : NULL;
            set_str(c->bm_gateways[i], sizeof(c->bm_gateways[i]), (v && cJSON_IsString(v)) ? v->valuestring : c->bm_gateways[i]);
        }
    }

    // The interlock the BrandMeister page enforces on save is re-applied here,
    // for the same reason every other bounded field is re-checked on load: a
    // config.json edited by hand or carried over from another station can
    // arrive with the worldwide monitor subscription on and nothing standing
    // between that feed and the transmitter. Turning the monitor flag off
    // rather than the gating is the conservative direction - it withdraws the
    // subscription the operator would otherwise be told to add, and leaves
    // every other setting as written.
    if (c->bm_monitor && c->inet2rf && !c->inet2rf_range_en) {
        ESP_LOGW(TAG, "bmMonitor requires the INET->RF range gate while inet2rf is on - monitor disabled");
        c->bm_monitor = false;
    }
    c->inet2rf_3rdparty_unwrap_en = jget_bool(d, "inet2rf3rdPartyUnwrapEn", c->inet2rf_3rdparty_unwrap_en);
    c->igate_msg_gate_en = jget_bool(d, "igateMsgGateEn", c->igate_msg_gate_en);
    // Same two-layer clamp the rest of the bounded fields use: the file on
    // flash is not a trusted input, and a window of zero would stop the
    // gateway putting any message on the air while a window of days would
    // keep transmitting to stations that left the area hours ago.
    c->igate_local_window_sec = (uint16_t)jget_num(d, "igateLocalWindowSec", c->igate_local_window_sec);
    if (c->igate_local_window_sec < IGATE_LOCAL_WINDOW_SEC_MIN || c->igate_local_window_sec > IGATE_LOCAL_WINDOW_SEC_MAX) {
        ESP_LOGW(TAG, "igateLocalWindowSec %u out of range, clamped to %d..%d s", (unsigned)c->igate_local_window_sec, IGATE_LOCAL_WINDOW_SEC_MIN,
                 IGATE_LOCAL_WINDOW_SEC_MAX);
        c->igate_local_window_sec = (c->igate_local_window_sec < IGATE_LOCAL_WINDOW_SEC_MIN) ? IGATE_LOCAL_WINDOW_SEC_MIN : IGATE_LOCAL_WINDOW_SEC_MAX;
    }
    // The hop limit takes the same treatment, read through an int so a stored
    // value outside the field's own range is caught before the narrowing cast
    // rather than after it: a limit longer than an AX.25 path can carry would
    // gate to stations no transmission from here can reach.
    {
        int maxHops = (int)jget_num(d, "igateMsgMaxHops", c->igate_msg_max_hops);
        if (maxHops < IGATE_MSG_MAX_HOPS_MIN || maxHops > IGATE_MSG_MAX_HOPS_MAX) {
            ESP_LOGW(TAG, "igateMsgMaxHops %d out of range, clamped to %d..%d hops", maxHops, IGATE_MSG_MAX_HOPS_MIN, IGATE_MSG_MAX_HOPS_MAX);
            maxHops = (maxHops < IGATE_MSG_MAX_HOPS_MIN) ? IGATE_MSG_MAX_HOPS_MIN : IGATE_MSG_MAX_HOPS_MAX;
        }
        c->igate_msg_max_hops = (uint8_t)maxHops;
    }
    c->aprs_ssid = (uint8_t)jget_num(d, "igateSSID", c->aprs_ssid);
    {
        cJSON *arr = cJSON_GetObjectItemCaseSensitive(d, "igateServers");
        if (arr && cJSON_IsArray(arr)) {
            for (int i = 0; i < APRS_SERVER_NUM; i++) {
                cJSON *e = cJSON_GetArrayItem(arr, i * 3);
                cJSON *h = cJSON_GetArrayItem(arr, i * 3 + 1);
                cJSON *p = cJSON_GetArrayItem(arr, i * 3 + 2);
                c->aprs_server[i].enable = e ? cJSON_IsTrue(e) : c->aprs_server[i].enable;
                set_str(c->aprs_server[i].host, sizeof(c->aprs_server[i].host), (h && cJSON_IsString(h)) ? h->valuestring : c->aprs_server[i].host);
                c->aprs_server[i].port = (uint16_t)((p && cJSON_IsNumber(p)) ? p->valuedouble : c->aprs_server[i].port);
                // Same two-layer clamp as the SoftAP channel above: the file
                // on flash is not a trusted input, and port 0 would send the
                // IGate into a reconnect loop against an address it can
                // never connect to. Only the low bound needs testing:
                // APRS_PORT_MAX is the full range of the uint16_t the value
                // is already narrowed to.
                if (c->aprs_server[i].port < APRS_PORT_MIN) {
                    ESP_LOGW(TAG, "stored APRS-IS server %d port %u outside %u-%u, using %u", i, (unsigned)c->aprs_server[i].port, (unsigned)APRS_PORT_MIN,
                             (unsigned)APRS_PORT_MAX, (unsigned)APRS_PORT_DEFAULT);
                    c->aprs_server[i].port = APRS_PORT_DEFAULT;
                }
            }
        } else {
            // Pre-failover config.json: migrate the single legacy "igateHost"
            // / "igatePort" pair into slot 0 so an upgraded device keeps
            // connecting to the same server it already had configured. Every
            // slot still holds the default written at the top of this
            // function - nothing in this branch has touched the array yet -
            // so only slot 0 needs filling in from the legacy keys.
            set_str(c->aprs_server[0].host, sizeof(c->aprs_server[0].host), jget_str(d, "igateHost", c->aprs_server[0].host));
            c->aprs_server[0].port = (uint16_t)jget_num(d, "igatePort", c->aprs_server[0].port);
            if (c->aprs_server[0].port < APRS_PORT_MIN) {
                ESP_LOGW(TAG, "stored APRS-IS port %u outside %u-%u, using %u", (unsigned)c->aprs_server[0].port, (unsigned)APRS_PORT_MIN,
                         (unsigned)APRS_PORT_MAX, (unsigned)APRS_PORT_DEFAULT);
                c->aprs_server[0].port = APRS_PORT_DEFAULT;
            }
            c->aprs_server[0].enable = true;
        }
    }
    set_str(c->aprs_mycall, sizeof(c->aprs_mycall), jget_str(d, "igateMycall", c->aprs_mycall));
    c->igate_use_station = jget_bool(d, "igateUseStation", c->igate_use_station);
    c->igate_use_gps = jget_bool(d, "igateUseGps", c->igate_use_gps);
    set_str(c->aprs_passcode, sizeof(c->aprs_passcode), jget_str(d, "igatePasscode", c->aprs_passcode));
    set_str(c->aprs_filter, sizeof(c->aprs_filter), jget_str(d, "igateFilter", c->aprs_filter));
    c->igate_log_after_filters = jget_bool(d, "igateLogAfterFilters", c->igate_log_after_filters);
    c->igate_lat = (float)jget_num(d, "igateLAT", c->igate_lat);
    c->igate_lon = (float)jget_num(d, "igateLON", c->igate_lon);
    c->igate_alt = (float)jget_num(d, "igateALT", c->igate_alt);
    c->igate_interval = (uint16_t)jget_num(d, "igateINV", c->igate_interval);
    set_str(c->igate_symbol, sizeof(c->igate_symbol), jget_str(d, "igateSymbol", c->igate_symbol));
    clamp_symbol(c->igate_symbol, "igateSymbol");
    c->igate_path = (uint8_t)jget_num(d, "igatePath", c->igate_path);
    set_str_utf8(c->igate_comment, sizeof(c->igate_comment), jget_str(d, "igateComment", c->igate_comment));
    c->igate_timestamp = jget_bool(d, "igateTimestamp", c->igate_timestamp);
    c->igate_compress = jget_bool(d, "igateCompress", c->igate_compress);
    c->igate_phg_enable = jget_bool(d, "igatePHGEn", c->igate_phg_enable);
    c->igate_phg_use_station = jget_bool(d, "igatePHGUseStation", c->igate_phg_use_station);
    c->igate_phg_power = (uint16_t)jget_num(d, "igatePHGPower", c->igate_phg_power);
    c->igate_phg_gain = (float)jget_num(d, "igatePHGGain", c->igate_phg_gain);
    c->igate_phg_height = (uint16_t)jget_num(d, "igatePHGHeight", c->igate_phg_height);
    c->igate_phg_dir = (uint8_t)jget_num(d, "igatePHGDir", c->igate_phg_dir);
    c->igate_ext_type = (uint8_t)jget_num(d, "igateExtType", c->igate_ext_type);
    if (c->igate_ext_type > APRS_EXT_DF) {
        ESP_LOGW(TAG, "igateExtType %u unknown, using PHG", (unsigned)c->igate_ext_type);
        c->igate_ext_type = APRS_EXT_PHG;
    }
    c->igate_range_miles = (uint16_t)jget_num(d, "igateRng", c->igate_range_miles);
    if (c->igate_range_miles > APRS_EXT_RANGE_MILES_MAX) {
        ESP_LOGW(TAG, "igateRng %u out of range, clamped to %d", (unsigned)c->igate_range_miles, APRS_EXT_RANGE_MILES_MAX);
        c->igate_range_miles = APRS_EXT_RANGE_MILES_MAX;
    }
    c->igate_dfs_strength = (uint8_t)jget_num(d, "igateDfsS", c->igate_dfs_strength);
    if (c->igate_dfs_strength > APRS_EXT_DFS_STRENGTH_MAX) {
        ESP_LOGW(TAG, "igateDfsS %u out of range, clamped to %d", (unsigned)c->igate_dfs_strength, APRS_EXT_DFS_STRENGTH_MAX);
        c->igate_dfs_strength = APRS_EXT_DFS_STRENGTH_MAX;
    }
    {
        // The bearing wraps rather than clamps: 360 degrees and 0 degrees name
        // the same direction, and the on-air field is three digits, so a value
        // outside the range still has one correct reading.
        int brg = (int)jget_num(d, "igateDfBrg", c->igate_df_bearing);
        int wrapped = brg % 360;
        if (wrapped < 0)
            wrapped += 360;
        if (wrapped != brg)
            ESP_LOGW(TAG, "igateDfBrg %d out of range, wrapped to %d", brg, wrapped);
        c->igate_df_bearing = (uint16_t)wrapped;
    }
    c->igate_df_nrq_n = clamp_nrq_digit(jget_num(d, "igateDfN", c->igate_df_nrq_n), "igateDfN");
    c->igate_df_nrq_r = clamp_nrq_digit(jget_num(d, "igateDfR", c->igate_df_nrq_r), "igateDfR");
    c->igate_df_nrq_q = clamp_nrq_digit(jget_num(d, "igateDfQ", c->igate_df_nrq_q), "igateDfQ");
    c->igate_freq_mhz = (float)jget_num(d, "igateFreqMHz", c->igate_freq_mhz);
    c->igate_tone_tenths = (uint16_t)jget_num(d, "igateFreqTone", c->igate_tone_tenths);
    c->igate_duplex = (int8_t)jget_num(d, "igateFreqDup", c->igate_duplex);
    c->igate_offset_khz = (uint16_t)jget_num(d, "igateFreqOff", c->igate_offset_khz);
    c->igate_sts_interval = (uint16_t)jget_num(d, "igateSTSIntv", c->igate_sts_interval);
    set_str_utf8(c->igate_status, sizeof(c->igate_status), jget_str(d, "igateStatus", c->igate_status));

    c->digi_en = jget_bool(d, "digiEn", c->digi_en);
    c->digi_loc2rf = jget_bool(d, "digiPos2rf", c->digi_loc2rf);
    c->digi_loc2inet = jget_bool(d, "digiPos2inet", c->digi_loc2inet);
    c->digi_timestamp = jget_bool(d, "digiTime", c->digi_timestamp);
    c->digi_ssid = (uint8_t)jget_num(d, "digiSSID", c->digi_ssid);
    set_str(c->digi_mycall, sizeof(c->digi_mycall), jget_str(d, "digiMycall", c->digi_mycall));
    c->digi_use_station = jget_bool(d, "digiUseStation", c->digi_use_station);
    c->digi_use_gps = jget_bool(d, "digiUseGps", c->digi_use_gps);
    c->digi_path = (uint8_t)jget_num(d, "digiPath", c->digi_path);
    // Alias table: three parallel arrays, one row per index, following the
    // same shape as the budlist/satgate lists above. A row is validated on the
    // way in rather than trusted: an out-of-range hop limit or an unknown mode
    // would otherwise decide how this station repeats other people's traffic.
    {
        cJSON *al = cJSON_GetObjectItemCaseSensitive(d, "digiAlias");
        cJSON *an = cJSON_GetObjectItemCaseSensitive(d, "digiAliasMaxN");
        cJSON *am = cJSON_GetObjectItemCaseSensitive(d, "digiAliasMode");
        for (int i = 0; i < DIGI_ALIAS_MAX; i++) {
            cJSON *v = al ? cJSON_GetArrayItem(al, i) : NULL;
            set_str(c->digi_alias[i].alias, sizeof(c->digi_alias[i].alias), (v && cJSON_IsString(v)) ? v->valuestring : c->digi_alias[i].alias);

            cJSON *n = an ? cJSON_GetArrayItem(an, i) : NULL;
            c->digi_alias[i].max_n = (uint8_t)((n && cJSON_IsNumber(n)) ? n->valuedouble : c->digi_alias[i].max_n);
            if (c->digi_alias[i].max_n < 1 || c->digi_alias[i].max_n > DIGI_ALIAS_MAX_N) {
                ESP_LOGW(TAG, "digiAliasMaxN[%d] %u out of range, clamped to 1..%d", i, (unsigned)c->digi_alias[i].max_n, DIGI_ALIAS_MAX_N);
                c->digi_alias[i].max_n = (c->digi_alias[i].max_n < 1) ? 1 : DIGI_ALIAS_MAX_N;
            }

            cJSON *m = am ? cJSON_GetArrayItem(am, i) : NULL;
            c->digi_alias[i].mode = (uint8_t)((m && cJSON_IsNumber(m)) ? m->valuedouble : c->digi_alias[i].mode);
            if (c->digi_alias[i].mode != DIGI_ALIAS_OFF && c->digi_alias[i].mode != DIGI_ALIAS_TRACE && c->digi_alias[i].mode != DIGI_ALIAS_FLOOD) {
                ESP_LOGW(TAG, "digiAliasMode[%d] %u unknown, row disabled", i, (unsigned)c->digi_alias[i].mode);
                c->digi_alias[i].mode = DIGI_ALIAS_OFF;
            }
        }
    }
    c->digi_fillin_only = jget_bool(d, "digiFillinOnly", c->digi_fillin_only);
    c->digi_trap_n_clamp = jget_bool(d, "digiTrapNClamp", c->digi_trap_n_clamp);
    c->digi_dest_ssid_en = jget_bool(d, "digiDestSsidEn", c->digi_dest_ssid_en);
    c->digi_preempt = (uint8_t)jget_num(d, "digiPreempt", c->digi_preempt);
    if (c->digi_preempt != DIGI_PREEMPT_OFF && c->digi_preempt != DIGI_PREEMPT_DROP && c->digi_preempt != DIGI_PREEMPT_MARK) {
        ESP_LOGW(TAG, "digiPreempt %u unknown, preemptive digipeating disabled", (unsigned)c->digi_preempt);
        c->digi_preempt = DIGI_PREEMPT_OFF;
    }
    c->digi_bcn = jget_bool(d, "digiBcn", c->digi_bcn);
    c->digi_compress = jget_bool(d, "digiCompress", c->digi_compress);
    c->digi_alt = (float)jget_num(d, "digiAlt", c->digi_alt);
    c->digi_lat = (float)jget_num(d, "digiLAT", c->digi_lat);
    c->digi_lon = (float)jget_num(d, "digiLON", c->digi_lon);
    c->digi_interval = (uint16_t)jget_num(d, "digiINV", c->digi_interval);
    set_str(c->digi_symbol, sizeof(c->digi_symbol), jget_str(d, "digiSymbol", c->digi_symbol));
    clamp_symbol(c->digi_symbol, "digiSymbol");
    set_str_utf8(c->digi_comment, sizeof(c->digi_comment), jget_str(d, "digiComment", c->digi_comment));
    c->digi_sts_interval = (uint16_t)jget_num(d, "digiSTSIntv", c->digi_sts_interval);
    set_str_utf8(c->digi_status, sizeof(c->digi_status), jget_str(d, "digiStatus", c->digi_status));
    c->digi_phg_enable = jget_bool(d, "digiPHGEn", c->digi_phg_enable);
    c->digi_phg_use_station = jget_bool(d, "digiPHGUseStation", c->digi_phg_use_station);
    c->digi_phg_power = (uint16_t)jget_num(d, "digiPHGPower", c->digi_phg_power);
    c->digi_phg_gain = (float)jget_num(d, "digiPHGGain", c->digi_phg_gain);
    c->digi_phg_height = (uint16_t)jget_num(d, "digiPHGHeight", c->digi_phg_height);
    c->digi_phg_dir = (uint8_t)jget_num(d, "digiPHGDir", c->digi_phg_dir);
    c->digi_ext_type = (uint8_t)jget_num(d, "digiExtType", c->digi_ext_type);
    if (c->digi_ext_type > APRS_EXT_DF) {
        ESP_LOGW(TAG, "digiExtType %u unknown, using PHG", (unsigned)c->digi_ext_type);
        c->digi_ext_type = APRS_EXT_PHG;
    }
    c->digi_range_miles = (uint16_t)jget_num(d, "digiRng", c->digi_range_miles);
    if (c->digi_range_miles > APRS_EXT_RANGE_MILES_MAX) {
        ESP_LOGW(TAG, "digiRng %u out of range, clamped to %d", (unsigned)c->digi_range_miles, APRS_EXT_RANGE_MILES_MAX);
        c->digi_range_miles = APRS_EXT_RANGE_MILES_MAX;
    }
    c->digi_dfs_strength = (uint8_t)jget_num(d, "digiDfsS", c->digi_dfs_strength);
    if (c->digi_dfs_strength > APRS_EXT_DFS_STRENGTH_MAX) {
        ESP_LOGW(TAG, "digiDfsS %u out of range, clamped to %d", (unsigned)c->digi_dfs_strength, APRS_EXT_DFS_STRENGTH_MAX);
        c->digi_dfs_strength = APRS_EXT_DFS_STRENGTH_MAX;
    }
    {
        // The bearing wraps rather than clamps: 360 degrees and 0 degrees name
        // the same direction, and the on-air field is three digits, so a value
        // outside the range still has one correct reading.
        int brg = (int)jget_num(d, "digiDfBrg", c->digi_df_bearing);
        int wrapped = brg % 360;
        if (wrapped < 0)
            wrapped += 360;
        if (wrapped != brg)
            ESP_LOGW(TAG, "digiDfBrg %d out of range, wrapped to %d", brg, wrapped);
        c->digi_df_bearing = (uint16_t)wrapped;
    }
    c->digi_df_nrq_n = clamp_nrq_digit(jget_num(d, "digiDfN", c->digi_df_nrq_n), "digiDfN");
    c->digi_df_nrq_r = clamp_nrq_digit(jget_num(d, "digiDfR", c->digi_df_nrq_r), "digiDfR");
    c->digi_df_nrq_q = clamp_nrq_digit(jget_num(d, "digiDfQ", c->digi_df_nrq_q), "digiDfQ");
    c->digi_freq_mhz = (float)jget_num(d, "digiFreqMHz", c->digi_freq_mhz);
    c->digi_tone_tenths = (uint16_t)jget_num(d, "digiFreqTone", c->digi_tone_tenths);
    c->digi_duplex = (int8_t)jget_num(d, "digiFreqDup", c->digi_duplex);
    c->digi_offset_khz = (uint16_t)jget_num(d, "digiFreqOff", c->digi_offset_khz);

    c->trk_en = jget_bool(d, "trkEn", c->trk_en);
    c->trk_loc2rf = jget_bool(d, "trkPos2rf", c->trk_loc2rf);
    c->trk_loc2inet = jget_bool(d, "trkPos2inet", c->trk_loc2inet);
    c->trk_timestamp = jget_bool(d, "trkTime", c->trk_timestamp);
    c->trk_ssid = (uint8_t)jget_num(d, "trkSSID", c->trk_ssid);
    set_str(c->trk_mycall, sizeof(c->trk_mycall), jget_str(d, "trkMycall", c->trk_mycall));
    c->trk_use_station = jget_bool(d, "trkUseStation", c->trk_use_station);
    c->trk_use_gps = jget_bool(d, "trkUseGps", c->trk_use_gps);
    c->trk_use_live_gps = jget_bool(d, "trkUseLiveGps", c->trk_use_live_gps);
    c->trk_path = (uint8_t)jget_num(d, "trkPath", c->trk_path);
    c->trk_lat = (float)jget_num(d, "trkLAT", c->trk_lat);
    c->trk_lon = (float)jget_num(d, "trkLON", c->trk_lon);
    c->trk_alt = (float)jget_num(d, "trkALT", c->trk_alt);
    c->trk_interval = (uint16_t)jget_num(d, "trkINV", c->trk_interval);
    c->trk_compress = jget_bool(d, "trkCompress", c->trk_compress);
    c->trk_phg_enable = jget_bool(d, "trkPHG", c->trk_phg_enable);
    c->trk_mice = jget_bool(d, "trkMice", c->trk_mice);
    c->trk_mice_msg = (uint8_t)jget_num(d, "trkMiceMsg", c->trk_mice_msg);
    // Same two-layer clamp every other bounded field uses: the form handler
    // bounds what the operator can send, and this bounds what a hand-edited
    // or older config.json can carry into the beacon builder.
    if (c->trk_mice_msg > MICE_POS_COMMENT_MAX) {
        ESP_LOGW(TAG, "trkMiceMsg %u out of range (0-%d) - using %d", (unsigned)c->trk_mice_msg, MICE_POS_COMMENT_MAX, MICE_POS_COMMENT_DEFAULT);
        c->trk_mice_msg = MICE_POS_COMMENT_DEFAULT;
    }
    c->trk_altitude = jget_bool(d, "trkOptAlt", c->trk_altitude);
    set_str(c->trk_symbol, sizeof(c->trk_symbol), jget_str(d, "trkSymbol", c->trk_symbol));
    clamp_symbol(c->trk_symbol, "trkSymbol");
    set_str_utf8(c->trk_comment, sizeof(c->trk_comment), jget_str(d, "trkComment", c->trk_comment));
    c->trk_sts_interval = (uint16_t)jget_num(d, "trkSTSIntv", c->trk_sts_interval);
    set_str_utf8(c->trk_status, sizeof(c->trk_status), jget_str(d, "trkStatus", c->trk_status));
    c->trk_freq_mhz = (float)jget_num(d, "trkFreqMHz", c->trk_freq_mhz);
    c->trk_tone_tenths = (uint16_t)jget_num(d, "trkFreqTone", c->trk_tone_tenths);
    c->trk_duplex = (int8_t)jget_num(d, "trkFreqDup", c->trk_duplex);
    c->trk_offset_khz = (uint16_t)jget_num(d, "trkFreqOff", c->trk_offset_khz);

    c->trk_sb_enable = jget_bool(d, "trkSbEn", c->trk_sb_enable);
    c->trk_sb_slow_interval =
        clamp_u16_range(jget_num(d, "trkSbSlowIntv", c->trk_sb_slow_interval), TRK_SB_SLOW_INTERVAL_S_MIN, TRK_SB_SLOW_INTERVAL_S_MAX, "trkSbSlowIntv");
    c->trk_sb_fast_interval =
        clamp_u16_range(jget_num(d, "trkSbFastIntv", c->trk_sb_fast_interval), TRK_SB_FAST_INTERVAL_S_MIN, TRK_SB_FAST_INTERVAL_S_MAX, "trkSbFastIntv");
    c->trk_sb_low_speed_kmh = clamp_u16_range(jget_num(d, "trkSbLowSpd", c->trk_sb_low_speed_kmh), TRK_SB_SPEED_KMH_MIN, TRK_SB_SPEED_KMH_MAX, "trkSbLowSpd");
    c->trk_sb_high_speed_kmh =
        clamp_u16_range(jget_num(d, "trkSbHighSpd", c->trk_sb_high_speed_kmh), TRK_SB_SPEED_KMH_MIN, TRK_SB_SPEED_KMH_MAX, "trkSbHighSpd");
    if (c->trk_sb_high_speed_kmh < c->trk_sb_low_speed_kmh) {
        ESP_LOGW(TAG, "trkSbHighSpd %u below trkSbLowSpd %u, raised to match", (unsigned)c->trk_sb_high_speed_kmh, (unsigned)c->trk_sb_low_speed_kmh);
        c->trk_sb_high_speed_kmh = c->trk_sb_low_speed_kmh;
    }
    c->trk_sb_turn_angle = clamp_u16_range(jget_num(d, "trkSbTurnAngle", c->trk_sb_turn_angle), TRK_SB_TURN_ANGLE_MIN, TRK_SB_TURN_ANGLE_MAX, "trkSbTurnAngle");
    c->trk_sb_turn_slope = clamp_u16_range(jget_num(d, "trkSbTurnSlope", c->trk_sb_turn_slope), TRK_SB_TURN_SLOPE_MIN, TRK_SB_TURN_SLOPE_MAX, "trkSbTurnSlope");
    c->trk_sb_min_turn_time =
        clamp_u16_range(jget_num(d, "trkSbMinTurnTime", c->trk_sb_min_turn_time), TRK_SB_MIN_TURN_TIME_S_MIN, TRK_SB_MIN_TURN_TIME_S_MAX, "trkSbMinTurnTime");

    c->gps_en = jget_bool(d, "gpsEn", c->gps_en);

    c->wx_en = jget_bool(d, "wxEn", c->wx_en);
    c->wx_2rf = jget_bool(d, "wxTx2rf", c->wx_2rf);
    c->wx_2inet = jget_bool(d, "wxTx2inet", c->wx_2inet);
    c->wx_timestamp = jget_bool(d, "wxTime", c->wx_timestamp);
    c->wx_ssid = (uint8_t)jget_num(d, "wxSSID", c->wx_ssid);
    set_str(c->wx_mycall, sizeof(c->wx_mycall), jget_str(d, "wxMycall", c->wx_mycall));
    c->wx_use_station = jget_bool(d, "wxUseStation", c->wx_use_station);
    c->wx_use_gps = jget_bool(d, "wxUseGps", c->wx_use_gps);
    c->wx_path = (uint8_t)jget_num(d, "wxPath", c->wx_path);
    c->wx_lat = (float)jget_num(d, "wxLAT", c->wx_lat);
    c->wx_lon = (float)jget_num(d, "wxLON", c->wx_lon);
    c->wx_interval = (uint16_t)jget_num(d, "wxInv", c->wx_interval);
    set_str(c->wx_object, sizeof(c->wx_object), jget_str(d, "wxObject", c->wx_object));
    set_str_utf8(c->wx_comment, sizeof(c->wx_comment), jget_str(d, "wxComment", c->wx_comment));
    {
        cJSON *a1 = cJSON_GetObjectItemCaseSensitive(d, "wxSenEn"), *a2 = cJSON_GetObjectItemCaseSensitive(d, "wxSenAvg"),
              *a3 = cJSON_GetObjectItemCaseSensitive(d, "wxSenCH");
        for (int i = 0; i < WX_SENSOR_NUM; i++) {
            cJSON *v;
            if (a1 && (v = cJSON_GetArrayItem(a1, i)))
                c->wx_sensor_enable[i] = cJSON_IsTrue(v);
            if (a2 && (v = cJSON_GetArrayItem(a2, i)))
                c->wx_sensor_avg[i] = cJSON_IsTrue(v);
            if (a3 && (v = cJSON_GetArrayItem(a3, i))) {
                if (cJSON_IsString(v)) {
                    // Driver name (see the writer above): resolve it against
                    // the registry this image actually built. A name that is
                    // no longer registered leaves the field unmapped and says
                    // so, rather than aiming it at whatever sensor now sits at
                    // that position.
                    c->wx_sensor_ch[i] = sensors_local_channel_from_name(v->valuestring);
                    if (c->wx_sensor_ch[i] == SENSOR_LOCAL_CH_NONE && v->valuestring != NULL && v->valuestring[0] != 0)
                        ESP_LOGW(TAG, "WX field %d: sensor '%s' is not registered, left unmapped", i, v->valuestring);
                } else if (cJSON_IsNumber(v)) {
                    // A config.json that predates name-based mappings: the
                    // number is a registry position, and it is only meaningful
                    // if the registry still reaches that far.
                    unsigned idx = (unsigned)v->valuedouble;
                    if (idx != SENSOR_LOCAL_CH_NONE && idx >= sensors_local_count()) {
                        ESP_LOGW(TAG, "WX field %d: stored sensor channel %u no longer exists, left unmapped", i, idx);
                        idx = SENSOR_LOCAL_CH_NONE;
                    }
                    c->wx_sensor_ch[i] = (uint8_t)idx;
                }
            }
        }
    }

    // Telemetry configuration (channel 0/1, Binary B1-B8 mapping) is no
    // longer part of config.json - loaded separately via
    // telemetry_config_load() from /storage/telemetry.json. Any leftover
    // tlm0*/tlm1*/tlmBit* keys in an old config.json are simply ignored here
    // (config_from_json() already ignores unknown keys generally).

    set_str(c->http_username, sizeof(c->http_username), jget_str(d, "httpUser", c->http_username));
    set_str(c->http_password, sizeof(c->http_password), jget_str(d, "httpPass", c->http_password));
    {
        cJSON *p = cJSON_GetObjectItemCaseSensitive(d, "path");
        for (int i = 0; i < 4; i++) {
            cJSON *v = p ? cJSON_GetArrayItem(p, i) : NULL;
            set_str(c->path[i], sizeof(c->path[i]), (v && cJSON_IsString(v)) ? v->valuestring : c->path[i]);
        }
    }

    // rfPTT (PTT GPIO) and rfPTTAct (PTT active-high) are not read back:
    // a config.json may still contain either key, but config_from_json()
    // ignores unknown keys, so both are simply skipped - the values come
    // from MODEM_PTT_GPIO / MODEM_PTT_ACTIVE_HIGH instead.

    if (!cJSON_GetObjectItemCaseSensitive(d, "msgEnable")) {
        // old-version file compatibility -> keep documented defaults
        c->msg_enable = true;
        c->msg_rf = true;
        c->msg_inet = true;
        c->msg_retry = 3;
        c->msg_interval = 30;
        c->msg_path = 9;
        set_str(c->msg_mycall, sizeof(c->msg_mycall), "NOCALL");
    } else {
        c->msg_enable = jget_bool(d, "msgEnable", c->msg_enable);
        c->msg_path = (uint8_t)jget_num(d, "msgPath", c->msg_path);
        c->msg_rf = jget_bool(d, "msgRf", c->msg_rf);
        c->msg_inet = jget_bool(d, "msgInet", c->msg_inet);
        c->msg_retry = (uint8_t)jget_num(d, "msgRetry", c->msg_retry);
        c->msg_interval = (uint16_t)jget_num(d, "msgInterval", c->msg_interval);
        set_str(c->msg_mycall, sizeof(c->msg_mycall), jget_str(d, "msgMycall", c->msg_mycall));
        c->msg_use_station = jget_bool(d, "msgUseStation", c->msg_use_station);
    }
    c->msg_alarm_enable = jget_bool(d, "msgAlarmEn", c->msg_alarm_enable);
    c->msg_alarm_gpio = (int8_t)jget_num(d, "msgAlarmGpio", c->msg_alarm_gpio);
    {
        cJSON *g = cJSON_GetObjectItemCaseSensitive(d, "msgGroup");
        for (int i = 0; i < 3; i++) {
            cJSON *v = g ? cJSON_GetArrayItem(g, i) : NULL;
            set_str(c->msg_group[i], sizeof(c->msg_group[i]), (v && cJSON_IsString(v)) ? v->valuestring : c->msg_group[i]);
        }
    }

    c->query_en = jget_bool(d, "queryEn", c->query_en);
    c->query_rf = jget_bool(d, "queryRf", c->query_rf);
    c->query_inet = jget_bool(d, "queryInet", c->query_inet);
    c->query_aprs_en = jget_bool(d, "queryAprsEn", c->query_aprs_en);
    c->query_wx_en = jget_bool(d, "queryWxEn", c->query_wx_en);
    c->query_igate_en = jget_bool(d, "queryIgateEn", c->query_igate_en);
    c->query_directed_en = jget_bool(d, "queryDirectedEn", c->query_directed_en);
    c->query_ext_en = jget_bool(d, "queryExtEn", c->query_ext_en);
    c->query_min_interval_sec = (uint16_t)jget_num(d, "queryMinInterval", c->query_min_interval_sec);
    if (c->query_min_interval_sec < 5) // floor: airtime/loop safety, matches the webconfig page's own clamp
        c->query_min_interval_sec = 5;
    c->query_cap_beacon_en = jget_bool(d, "queryCapEn", c->query_cap_beacon_en);
    {
        int iv = (int)jget_num(d, "queryCapIntv", c->query_cap_interval_sec);
        if (iv < QUERY_CAP_INTERVAL_S_MIN || iv > QUERY_CAP_INTERVAL_S_MAX) {
            int clamped = (iv < QUERY_CAP_INTERVAL_S_MIN) ? QUERY_CAP_INTERVAL_S_MIN : QUERY_CAP_INTERVAL_S_MAX;
            ESP_LOGW(TAG, "queryCapIntv %d out of range, clamped to %d", iv, clamped);
            iv = clamped;
        }
        c->query_cap_interval_sec = (uint32_t)iv;
    }
    c->query_cap_rf = jget_bool(d, "queryCapRf", c->query_cap_rf);
    c->query_cap_inet = jget_bool(d, "queryCapInet", c->query_cap_inet);
    set_str_utf8(c->query_cap_extra, sizeof(c->query_cap_extra), jget_str(d, "queryCapExtra", c->query_cap_extra));
    app_config_query_cap_extra_sanitize(c->query_cap_extra);
}

bool app_config_save(void) {
    // Streams the configuration straight to the file, one field at a time -
    // see the note on the JSON writer above for why no in-RAM document is
    // built.
    //
    // Serialize the whole save against any other save/load in flight (see
    // s_config_mutex comment above). Block indefinitely: a save must never
    // be silently dropped, and the critical section below is short.
    //
    // The handle is taken once and reused for the rest of the function, so the
    // stream handed out by json_store_open_tmp() is checked against the very
    // mutex this task holds.
    SemaphoreHandle_t lock = config_mutex();
    if (!lock) {
        ESP_LOGE(TAG, "save mutex unavailable (out of memory), configuration not written");
        return false;
    }
    xSemaphoreTake(lock, portMAX_DELAY);

    // Second, filesystem-wide gate (storage.h): config_mutex() only keeps two
    // config saves apart, while the temp-file + rename sequence below must
    // also not overlap the whole-partition erase the web Storage page can
    // trigger, nor a save being made by another subsystem. Module lock first,
    // this gate second - the order storage.h's contract requires.
    storage_write_lock();

    // The save mutex is held across this whole function, which is what
    // json_store_open_tmp() asserts before handing back a stream whose stdio
    // buffer is already pinned.
    FILE *f = json_store_open_tmp(CONFIG_TMP_PATH, TAG, lock);
    if (!f) {
        storage_write_unlock();
        xSemaphoreGive(lock);
        return false;
    }

    jw_t w = { .f = f, .obj_comma = false, .arr_comma = false };
    config_write_json(&w, &g_config);

    if (!json_store_commit(f, CONFIG_TMP_PATH, CONFIG_PATH, TAG, "configuration")) {
        storage_write_unlock();
        xSemaphoreGive(lock);
        return false;
    }

    // How close the calling task (normally the httpd task) came to overflowing
    // its stack during this save, so the config.stack_size in web_server.c can
    // be sized from real numbers instead of a guess. Remove once a safe margin
    // is confirmed.
    ESP_LOGI(TAG, "Caller stack high-water mark: %u bytes free", (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
    storage_write_unlock();
    xSemaphoreGive(lock);
    return true;
}

bool app_config_load(void) {
    cJSON *doc = NULL;
    json_store_status_t st = json_store_read(CONFIG_PATH, TAG, "configuration", &doc);

    switch (st) {
        case JSON_STORE_OK:
            break;

        case JSON_STORE_OOM:
            // The file is very probably intact - there was simply no RAM to
            // read it into. Leave it exactly as it is and report the failure,
            // rather than writing defaults over a configuration that a later
            // attempt would have loaded fine.
            return false;

        case JSON_STORE_MISSING:
        case JSON_STORE_EMPTY:
        case JSON_STORE_CORRUPT:
        default:
            // The boot configuration is the one file the device cannot come up
            // without, so anything unusable here is replaced with the factory
            // set and written back immediately. That costs an operator a
            // corrupt config.json they might have wanted to inspect, and buys
            // a station that always boots into a reachable web admin instead
            // of one that needs a serial flash to recover.
            ESP_LOGW(TAG, "%s unusable, writing defaults", CONFIG_PATH);
            app_config_set_defaults(&g_config);
            return app_config_save();
    }

    config_from_json(doc, &g_config);
    cJSON_Delete(doc);
    ESP_LOGI(TAG, "Configuration loaded");
    return true;
}

bool app_config_factory_reset(void) {
    // Web-reachable (/default) at runtime, so it rewrites the whole g_config
    // out from under the beacon/igate/message tasks. Hold the data lock across
    // the wholesale rewrite so no reader can sample a half-reset struct; drop
    // it before the (slow, self-locking) flash save.
    app_config_lock();
    app_config_set_defaults(&g_config);
    app_config_unlock();
    return app_config_save();
}

uint8_t app_config_path_preset_hops(const char *preset) {
    if (preset == NULL || !preset[0])
        return 0;

    uint8_t hops = 1; // the alias itself is at least 1 hop
    for (const char *p = preset; *p; p++)
        if (*p == ',')
            hops++;
    return hops;
}

uint8_t app_config_path_hop_count(uint8_t pathBitmask, const char pathPreset[4][72]) {
    uint8_t hops = 0;
    for (int bit = 0; bit < 4; bit++) {
        if (!(pathBitmask & (1u << bit)))
            continue;
        hops += app_config_path_preset_hops(pathPreset[bit]);
    }
    return hops;
}

uint8_t app_config_path_mask_clamp(uint8_t pathBitmask, const char pathPreset[4][72]) {
    if (app_config_path_hop_count(pathBitmask, pathPreset) <= 8)
        return pathBitmask; // within budget already - nothing to drop

    // Over AX.25's 8-via limit once every selected preset's own comma-joined
    // hops are counted (e.g. two 4-hop presets can exceed 8 well before all 4
    // checkboxes are ticked). Keep presets low-bit-first until the budget is
    // used up and drop the rest, instead of silently saving a bitmask that
    // would later be clipped differently (or reach further than intended) at
    // transmit time - see aprs_path_build_suffix(), which enforces the same
    // limit again as a belt-and-suspenders check for a configuration that
    // reached the device without passing through this form.
    uint8_t clamped = 0;
    uint8_t hopsUsed = 0;
    for (int bit = 0; bit < 4; bit++) {
        if (!(pathBitmask & (1u << bit)) || !pathPreset[bit][0])
            continue;

        uint8_t presetHops = 1;
        for (const char *p = pathPreset[bit]; *p; p++)
            if (*p == ',')
                presetHops++;

        if (hopsUsed + presetHops > 8) {
            ESP_LOGW(TAG, "path bitmask 0x%02X exceeds AX.25 8-hop limit, dropping preset %d and beyond", pathBitmask, bit + 1);
            break;
        }
        clamped |= (uint8_t)(1u << bit);
        hopsUsed += presetHops;
    }
    return clamped;
}
