// @file main.c
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
// @brief Firmware entry point: NVS/LittleFS bring-up, configuration load, WiFi
// station/AP setup and event handling, and creation of the application task that
// starts the CPU frequency policy, SNTP client, web admin server, APRS services
// and the radio modem.

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "app_config.h"
#include "aprs_service.h"
#include "cpu_freq.h"
#include "esp32idf_radioamateur_modem.h"
#include "gps.h"
#include "net_state.h"
#include "storage.h"
#include "time_sync.h"
#include "web_server.h"

static const char *TAG = "main";

// app_main() runs on the system "main" task, whose stack size is fixed by
// CONFIG_ESP_MAIN_TASK_STACK_SIZE (3584 bytes by default) and is not meant to
// host heavy work. wifi_init()/web_server_start() (esp_netif, esp_wifi,
// esp_http_server, cJSON, etc.) can easily use several KB of stack between
// them, which does not fit there. Rather than grow a stack shared with the
// system, all of that work runs on the dedicated task created below, with an
// explicit and generous stack size of its own.
#define APP_TASK_STACK_SIZE 8192
#define APP_TASK_PRIORITY   5

// Pause between the two boot attempts at reading config.json. The failure
// app_config_load() reports is a read that could not be completed at all
// (typically no contiguous heap for the parse), so the retry is worth making
// only after the allocations made during early startup have settled.
#define CONFIG_LOAD_RETRY_DELAY_MS 250

// Fixed delay applied before every reconnect attempt after a STA disconnect.
#define RECONNECT_INTERVAL_MS 5000

// True when the configured wifi_mode includes a station interface AND a usable
// STA entry was actually found and pushed to the driver. Gates every automatic
// esp_wifi_connect(): without it, the WIFI_EVENT_STA_START handler below would
// also fire (and try to associate) when page_wireless.c's WiFi-scan handler
// temporarily flips an AP-only radio to AP+STA, which would fight the scan.
static bool s_staEnabled = false;

// One-shot timer used to defer a reconnect attempt, rather than a vTaskDelay()
// inside the event handler: event handlers run on the shared event-loop task,
// so sleeping there would stall every other event - including the
// IP_EVENT_STA_GOT_IP this code is waiting for, and the AP's own events while
// in AP+STA mode.
static esp_timer_handle_t s_reconnectTimer = NULL;

static void try_connect(const char *why) {
    if (!s_staEnabled)
        return;
    esp_err_t err = esp_wifi_connect();
    // esp_wifi_connect() failing is the whole reason this function exists:
    // a connect that never starts never produces a WIFI_EVENT_STA_DISCONNECTED
    // either, so the retry path (which lives in the disconnect handler) would
    // never run and the station would sit dead and silent. Reporting the
    // result of every attempt makes that case visible and recoverable.
    if (err == ESP_OK)
        ESP_LOGI(TAG, "STA connect requested (%s)", why);
    else
        ESP_LOGE(TAG, "STA connect (%s) failed: %s", why, esp_err_to_name(err));
}

static void reconnect_timer_cb(void *arg) {
    try_connect("retry");
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base != WIFI_EVENT)
        return;

    if (id == WIFI_EVENT_STA_START) {
        // esp_wifi_connect() is only legal once the station interface has
        // actually started, and esp_wifi_start() signals that by posting this
        // event - it does not finish the job before returning. Calling
        // esp_wifi_connect() on the app task immediately after esp_wifi_start()
        // can lose that race and return ESP_ERR_WIFI_NOT_STARTED, with no
        // association and no WIFI_EVENT_STA_DISCONNECTED to trigger a retry
        // (the retry lives only in the disconnect handler). Connecting from
        // here is the order ESP-IDF guarantees.
        try_connect("STA_START");
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;

        // No AP link -> definitely no internet route until we reconnect AND
        // get a fresh IP (see ip_event_handler below).
        net_state_set_connected(false);

        // The reason code is the single most useful number when a station
        // won't associate: 15 (4WAY_HANDSHAKE_TIMEOUT) or 204 (NOT_AUTHED)
        // means the password is wrong, 201 (NO_AP_FOUND) means the SSID
        // isn't visible (wrong name, out of range, or 5 GHz-only), 2/8/200
        // are ordinary roaming/AP-side drops.
        ESP_LOGW(TAG, "STA disconnected from '%s', reason %d, retrying in %u ms...", (d && d->ssid_len) ? (const char *)d->ssid : "?", d ? (int)d->reason : -1,
                 (unsigned)RECONNECT_INTERVAL_MS);

        // Deferred, not slept: see the note on s_reconnectTimer. Every
        // disconnect - whether transient or persistent - waits the same
        // fixed interval before the next esp_wifi_connect() attempt.
        if (s_reconnectTimer) {
            esp_timer_stop(s_reconnectTimer); // no-op if not armed
            esp_timer_start_once(s_reconnectTimer, (uint64_t)RECONNECT_INTERVAL_MS * 1000ULL);
        }
    } else if (id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "Client connected to AP");
    }
}

// Fires once the STA interface actually has an IP (i.e. a real route to the
// internet, as opposed to merely being associated to the AP). This is the
// signal internet-dependent services (APRS-IS IGate) wait on before they
// attempt anything - see net_state.h.
static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "STA got IP: " IPSTR " - internet route available", IP2STR(&event->ip_info.ip));
        net_state_set_connected(true);
    }
}

// Applies the SoftAP settings from the running configuration.
//
// Nothing in here may abort. The access point is the only way back into the
// device when no station can associate, so a value the driver refuses has to
// end as a log line and a fallback, not as a panic: a panic lands before
// esp_wifi_start(), which leaves the device with no AP and no STA, rebooting
// into the same panic on every power cycle and recoverable only over serial.
//
// The channel is clamped on save and on load, so the retry below is the last
// line of defence for anything that still reaches the driver out of range.
static void wifi_apply_ap_config(void) {
    wifi_config_t ap_cfg = { 0 };
    size_t ssid_len = strnlen(g_config.wifi_ap_ssid, sizeof(ap_cfg.ap.ssid));
    memcpy(ap_cfg.ap.ssid, g_config.wifi_ap_ssid, ssid_len);
    ap_cfg.ap.ssid_len = (uint8_t)ssid_len;
    size_t pass_len = strnlen(g_config.wifi_ap_pass, sizeof(ap_cfg.ap.password));
    memcpy(ap_cfg.ap.password, g_config.wifi_ap_pass, pass_len);
    ap_cfg.ap.channel = g_config.wifi_ap_ch;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = strlen(g_config.wifi_ap_pass) >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    if (ap_cfg.ap.channel < WIFI_AP_CH_MIN || ap_cfg.ap.channel > WIFI_AP_CH_MAX) {
        ESP_LOGW(TAG, "SoftAP channel %u outside %u-%u, using %u", (unsigned)ap_cfg.ap.channel, (unsigned)WIFI_AP_CH_MIN, (unsigned)WIFI_AP_CH_MAX,
                 (unsigned)WIFI_AP_CH_DEFAULT);
        ap_cfg.ap.channel = WIFI_AP_CH_DEFAULT;
    }

    esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config(AP) rejected the stored settings: %s - retrying on channel %u", esp_err_to_name(err), (unsigned)WIFI_AP_CH_DEFAULT);
        ap_cfg.ap.channel = WIFI_AP_CH_DEFAULT;
        err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    }
    if (err != ESP_OK)
        ESP_LOGE(TAG, "esp_wifi_set_config(AP) failed: %s - the access point will not come up, fix the Wireless page over a station link or reflash",
                 esp_err_to_name(err));
}

static void wifi_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));

    // Map this project's interface selector onto the IDF mode enum. The two
    // numbering schemes are independent, so the mapping is spelled out rather
    // than cast.
    wifi_mode_t mode = WIFI_MODE_NULL;
    switch (g_config.wifi_mode) {
        case WIFI_MODE_CFG_STA:
            mode = WIFI_MODE_STA;
            break;
        case WIFI_MODE_CFG_AP:
            mode = WIFI_MODE_AP;
            break;
        case WIFI_MODE_CFG_APSTA:
            mode = WIFI_MODE_APSTA;
            break;
        default:
            // Covers WIFI_MODE_CFG_OFF and any value outside the selector
            // range: the SoftAP comes up regardless, so the web admin is
            // always reachable and no stored value can lock the operator out
            // of a headless station.
            mode = WIFI_MODE_AP;
            break;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(mode));

    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)
        wifi_apply_ap_config();

    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        for (int i = 0; i < WIFI_STA_NUM; i++) {
            if (g_config.wifi_sta[i].enable && g_config.wifi_sta[i].wifi_ssid[0]) {
                wifi_config_t sta_cfg = { 0 };
                strncpy((char *)sta_cfg.sta.ssid, g_config.wifi_sta[i].wifi_ssid, sizeof(sta_cfg.sta.ssid) - 1);
                strncpy((char *)sta_cfg.sta.password, g_config.wifi_sta[i].wifi_pass, sizeof(sta_cfg.sta.password) - 1);

                // Advertise PMF capability. A zeroed wifi_config_t leaves
                // pmf_cfg.capable = false, and an AP configured for WPA3 or
                // WPA2 with PMF required will simply refuse such a station -
                // it associates and is immediately dropped, or never gets
                // past the handshake. "capable, not required" is the setting
                // that works against both old and new APs.
                sta_cfg.sta.pmf_cfg.capable = true;
                sta_cfg.sta.pmf_cfg.required = false;

                // Same reasoning as the AP path: the station credentials also
                // come from stored data, and a set that the driver refuses
                // must not take the boot down. Log it and try the next slot,
                // so the access point still comes up either way.
                esp_err_t serr = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
                if (serr != ESP_OK) {
                    ESP_LOGE(TAG, "esp_wifi_set_config(STA) rejected slot %d ('%s'): %s - skipping it", i, g_config.wifi_sta[i].wifi_ssid,
                             esp_err_to_name(serr));
                    continue;
                }
                s_staEnabled = true;
                ESP_LOGI(TAG, "STA entry %d selected: SSID '%s'", i, g_config.wifi_sta[i].wifi_ssid);
                break; // first enabled entry; multi-AP failover can be added later
            }
        }

        // Loudly, because this is the other half of "I switched to STA and
        // nothing happened": if the loop above finds nothing, no STA config is
        // handed to the driver, and a bare esp_wifi_connect() would just return
        // ESP_ERR_WIFI_SSID into a discarded return value and fail silently.
        // Every WiFi Client block on the Wireless page has its own "Enable"
        // checkbox, separate from the Mode dropdown, and selecting Station mode
        // without ticking one leaves the station with nothing to connect to.
        if (!s_staEnabled) {
            ESP_LOGE(TAG, "wifi_mode=%u selects a station, but no WiFi Client entry is enabled with an SSID.", (unsigned)g_config.wifi_mode);
            // Dump every slot: "enabled but SSID empty" and "SSID set but not
            // enabled" are different mistakes with different fixes, and the
            // single summary line above cannot tell them apart.
            for (int i = 0; i < WIFI_STA_NUM; i++) {
                ESP_LOGE(TAG, "     slot %d: enable=%s ssid='%s'%s", i, g_config.wifi_sta[i].enable ? "true" : "false", g_config.wifi_sta[i].wifi_ssid,
                         (g_config.wifi_sta[i].enable && !g_config.wifi_sta[i].wifi_ssid[0])   ? "   <-- enabled, but the SSID is EMPTY"
                         : (!g_config.wifi_sta[i].enable && g_config.wifi_sta[i].wifi_ssid[0]) ? "   <-- has an SSID, but 'Enable' is not ticked"
                                                                                               : "");
            }
            ESP_LOGE(TAG, "  -> On the Wireless page, tick 'Enable' in a WiFi Client block and type an SSID, then Save.");
            if (mode == WIFI_MODE_STA) {
                // STA-only with nothing to join means no AP either: the device
                // would be unreachable over the air with no way back short of a
                // serial reflash. Fall back to AP+STA so the web admin stays up.
                ESP_LOGW(TAG, "  -> Falling back to AP+STA so the web admin stays reachable on '%s'.", g_config.wifi_ap_ssid);
                mode = WIFI_MODE_APSTA;
                ESP_ERROR_CHECK(esp_wifi_set_mode(mode));
                wifi_apply_ap_config();
            }
        }
    }

    // Armed by the disconnect handler, fired on the esp_timer task - never on
    // the event loop. Created before esp_wifi_start() so it exists by the time
    // the first event can arrive.
    if (s_staEnabled) {
        const esp_timer_create_args_t targs = {
            .callback = reconnect_timer_cb,
            .name = "wifi_reconnect",
        };
        ESP_ERROR_CHECK(esp_timer_create(&targs, &s_reconnectTimer));
    }

    ESP_ERROR_CHECK(esp_wifi_start());

    // No esp_wifi_connect() here any more - WIFI_EVENT_STA_START does it, once
    // the driver says the station is genuinely up. See the handler.

    // The units differ: the config field is dBm, esp_wifi_set_max_tx_power()
    // takes quarter-dBm, hence the x4. Both the form and config.json paths
    // clamp the stored value to WIFI_TX_POWER_DBM_MIN..MAX, so the product
    // always lands inside the range the driver accepts and fits an int8_t.
    // Only meaningful once the radio is started.
    int8_t qdbm = (int8_t)(g_config.wifi_power * 4);
    esp_err_t perr = esp_wifi_set_max_tx_power(qdbm);
    if (perr != ESP_OK)
        ESP_LOGW(TAG, "esp_wifi_set_max_tx_power(%d dBm) failed: %s", (int)g_config.wifi_power, esp_err_to_name(perr));

    ESP_LOGI(TAG, "WiFi started in mode %d (AP SSID '%s', STA %s)", (int)mode, g_config.wifi_ap_ssid, s_staEnabled ? "enabled" : "disabled");
}

// All of the actual application work happens here, on a task created with
// its own APP_TASK_STACK_SIZE stack, isolated from the system main task.
static void app_task(void *arg) {
    // Loads config.json, or writes+loads factory defaults if missing/corrupt.
    // A false return is the one case that leaves g_config untouched - the file
    // is believed intact but could not be read - so the whole station would
    // otherwise run from a zero-initialized struct: no callsign, no beacon
    // intervals, and an AP whose SSID is an empty string. Retry once, then put
    // the factory set in place so the device always comes up on a reachable
    // web admin the operator can correct it from.
    if (!app_config_load()) {
        ESP_LOGW(TAG, "Configuration load failed, retrying in %d ms", CONFIG_LOAD_RETRY_DELAY_MS);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_LOAD_RETRY_DELAY_MS));
        if (!app_config_load()) {
            ESP_LOGE(TAG, "Configuration could not be loaded, applying factory defaults");
            app_config_set_defaults(&g_config);
            // Persisting them is what makes the fallback survive the next
            // reset. Failing to do so is worth reporting but not worth
            // stopping the boot for: the defaults are already live in RAM.
            if (!app_config_save())
                ESP_LOGE(TAG, "Factory defaults could not be written, this boot runs from RAM only");
        }
    }

    // Apply the user-configured CPU frequency (System page) to the running
    // system, so the saved setting takes effect on the real clock and not only
    // in the UI.
    cpu_freq_apply();

    // Reset the "do we have internet" flag before bringing WiFi up; igate_start()
    // (below, via aprs_service_start()) polls this and waits for a real IP
    // before ever attempting an APRS-IS connection.
    net_state_init();

    wifi_init();
    // Yield here: wifi_init() leaves association/DHCP settling on this core,
    // and without a delay app_task can hog the CPU long enough (esp. AP+STA
    // mode) that IDLE1 never runs and the task watchdog fires a false alarm.
    vTaskDelay(pdMS_TO_TICKS(10));
    time_sync_start();

    // Bring the GNSS receiver up if the operator has it switched on. Done
    // before the web server so a page loaded immediately after boot already
    // finds the reader task running and reports the true link state instead of
    // "no data". The GPS page's save handler calls this again whenever the
    // switch moves, so enabling or disabling the receiver needs no reboot.
    gps_apply_config();

    web_server_start();

    // If this boot is running an image that the web admin's OTA Update
    // (About / Firmware page) just flashed, CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    // (see partitions.csv) leaves it in "pending verify" state: the bootloader
    // will silently roll back to the previous OTA slot on the *next* reset
    // unless something here confirms the image is good first. Reaching this
    // point means NVS/LittleFS mounted, WiFi came up, and the web admin is
    // listening - a reasonable bar for "this firmware works" - so confirm it.
    // On a single-"factory" partition table there is no pending-verify state
    // to find and this is a harmless no-op.
    {
        esp_ota_img_states_t ota_state;
        const esp_partition_t *running = esp_ota_get_running_partition();
        if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK && ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
            if (err == ESP_OK)
                ESP_LOGI(TAG, "OTA image confirmed valid on partition '%s' (rollback cancelled)", running->label);
            else
                ESP_LOGW(TAG, "esp_ota_mark_app_valid_cancel_rollback failed: %s", esp_err_to_name(err));
        }
    }

    // Bring up the AFSK/AX.25 modem + callsign/path settings from g_config,
    // then start the digipeater/igate/message application layer (aprs_service.c).
    //
    // The modem's runtime settings live in a modem_config_t, built from
    // g_config by aprs_service_build_modem_config() so the same mapping serves
    // here, the live re-apply on the Radio page, and the LOOP TEST (which only
    // flips full_duplex on top of it).
    //
    // What is deliberately NOT in that struct: the ADC, DAC and PTT pins and
    // the ADC attenuation are compile-time constants (see the
    // idf_build_set_property() block in the top-level CMakeLists.txt), and the
    // AGC gain ceiling and squelch threshold do not exist as settings at all -
    // the modem's AGC needs no ceiling and there is no software squelch, since
    // the AX.25 decoder gates on real DCD instead. None of them have g_config
    // fields, and none are selectable on the Radio/Modem page.
    //
    // aprs_service_start() must run before modem_init(): it installs the RX
    // callback, and the component starts delivering frames from inside
    // modem_init().
    aprs_service_start();

    // Only bring up the audio ADC/DAC AFSK modem hardware when it's enabled
    // on the Radio / Modem (Audio / AFSK) webconfig page.
    if (g_config.audio_modem_en) {
        modem_config_t modem_cfg;
        aprs_service_build_modem_config(&modem_cfg, false);

        // Note: modem_init() blocks for ~5 s while it measures this board's
        // real ADC sample rate (see ModemCalibrateSampleRate() in the
        // component). That is expected and happens exactly once per boot.
        esp_err_t err = modem_init(&modem_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "modem_init() failed: %s - RF TX/RX disabled", esp_err_to_name(err));
        } else {
            aprs_service_notify_modem_ready();
        }
    } else {
        ESP_LOGI(TAG, "Audio ADC/DAC AFSK modem disabled in config - skipping modem_init()");
    }

    // Do not log the admin password: this line reaches serial console captures
    // and any future remote-logging feature. Only the username is logged; the
    // operator already has the configured password.
    ESP_LOGI(TAG, APRS_SOFTWARE_NAME " web admin ready. Login user: %s", g_config.http_username);

    // Initialisation is done and everything above runs in its own tasks now
    // (WiFi, web server, APRS service + its tick, the beacon scheduler, the
    // modem RX/service tasks). app_task owns nothing beyond its own stack and
    // doesn't need to stay resident, so delete it instead of parking it in an
    // idle loop - this returns its APP_TASK_STACK_SIZE (8 KB) to the heap.
    vTaskDelete(NULL);
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (!storage_init()) {
        ESP_LOGE(TAG, "LittleFS mount failed - config cannot persist!");
    }

    BaseType_t ok = xTaskCreate(app_task, "app_task", APP_TASK_STACK_SIZE, NULL, APP_TASK_PRIORITY, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create app_task (out of memory?)");
    }

    // app_main can now return; the system main task frees its stack and
    // FreeRTOS deletes the task automatically (idf.py default behavior).
}
