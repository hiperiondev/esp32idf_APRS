#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "LibAPRSesp.h"
#include "app_config.h"
#include "aprs_service.h"
#include "cpu_freq.h"
#include "net_state.h"
#include "storage.h"
#include "time_sync.h"
#include "web_server.h"

static const char *TAG = "main";

/*
 * app_main() runs on the system "main" task, whose stack size is fixed by
 * CONFIG_ESP_MAIN_TASK_STACK_SIZE (3584 bytes by default) and is not meant
 * to host heavy work. wifi_init()/web_server_start() (esp_netif, esp_wifi,
 * esp_http_server, cJSON, etc.) can easily use several KB of stack between
 * them, which overflowed that task. To fix this properly (rather than just
 * growing the shared main-task stack), all of that work now runs in its own
 * task created with an explicit, generous stack size below.
 */
#define APP_TASK_STACK_SIZE 8192
#define APP_TASK_PRIORITY   5

// Consecutive-disconnect counter used to back off reconnect attempts below.
// Reset to 0 as soon as we get a real IP (see ip_event_handler).
static uint8_t s_disconnectStreak = 0;
#define RECONNECT_BACKOFF_STEP_MS 500  // grows by this much per consecutive failure
#define RECONNECT_BACKOFF_MAX_MS  8000 // ...up to this ceiling

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        // No AP link -> definitely no internet route until we reconnect AND
        // get a fresh IP (see ip_event_handler below).
        net_state_set_connected(false);

        // Back off before retrying: an AP that's out of range, has a wrong
        // password, or is momentarily down otherwise causes an immediate
        // disconnect -> esp_wifi_connect() -> immediate disconnect loop with
        // zero delay, which pins whatever core hosts the event-loop task and
        // starves its idle task (task watchdog trips). Growing the delay a
        // little on each consecutive failure (capped) keeps retries prompt
        // on transient blips while giving up CPU time between attempts.
        uint32_t backoffMs = (uint32_t)s_disconnectStreak * RECONNECT_BACKOFF_STEP_MS;
        if (backoffMs > RECONNECT_BACKOFF_MAX_MS)
            backoffMs = RECONNECT_BACKOFF_MAX_MS;
        if (s_disconnectStreak < 255)
            s_disconnectStreak++;

        ESP_LOGW(TAG, "STA disconnected, retrying in %u ms...", (unsigned)backoffMs);
        if (backoffMs > 0)
            vTaskDelay(pdMS_TO_TICKS(backoffMs));
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
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
        s_disconnectStreak = 0;
    }
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

    // wifi_mode: 0=off 1=STA 2=AP 3=AP+STA  (matches the web admin's Wireless page)
    wifi_mode_t mode = WIFI_MODE_NULL;
    switch (g_config.wifi_mode) {
        case 1:
            mode = WIFI_MODE_STA;
            break;
        case 2:
            mode = WIFI_MODE_AP;
            break;
        case 3:
            mode = WIFI_MODE_APSTA;
            break;
        default:
            mode = WIFI_MODE_AP;
            break; // safest default: always reachable via AP
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(mode));

    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        wifi_config_t ap_cfg = { 0 };
        strncpy((char *)ap_cfg.ap.ssid, g_config.wifi_ap_ssid, sizeof(ap_cfg.ap.ssid) - 1);
        ap_cfg.ap.ssid_len = strlen(g_config.wifi_ap_ssid);
        strncpy((char *)ap_cfg.ap.password, g_config.wifi_ap_pass, sizeof(ap_cfg.ap.password) - 1);
        ap_cfg.ap.channel = g_config.wifi_ap_ch;
        ap_cfg.ap.max_connection = 4;
        ap_cfg.ap.authmode = strlen(g_config.wifi_ap_pass) >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    }

    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        for (int i = 0; i < WIFI_STA_NUM; i++) {
            if (g_config.wifi_sta[i].enable && g_config.wifi_sta[i].wifi_ssid[0]) {
                wifi_config_t sta_cfg = { 0 };
                strncpy((char *)sta_cfg.sta.ssid, g_config.wifi_sta[i].wifi_ssid, sizeof(sta_cfg.sta.ssid) - 1);
                strncpy((char *)sta_cfg.sta.password, g_config.wifi_sta[i].wifi_pass, sizeof(sta_cfg.sta.password) - 1);
                ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
                break; // first enabled entry; multi-AP failover can be added later
            }
        }
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        esp_wifi_connect();
    }
    ESP_LOGI(TAG, "WiFi started in mode %d (AP SSID '%s')", (int)mode, g_config.wifi_ap_ssid);
}

/*
 * All of the actual application work happens here, on a task created with
 * its own APP_TASK_STACK_SIZE stack, isolated from the system main task.
 */
static void app_task(void *arg) {
    // Loads config.json, or writes+loads factory defaults if missing/corrupt.
    app_config_load();

    // Apply the user-configured CPU frequency (System page) to the running
    // system - without this the setting was only stored/displayed and never
    // actually changed the clock speed.
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
    web_server_start();

    // Bring up the AFSK/AX.25 modem + callsign/path settings from g_config,
    // then start the digipeater/igate/message application layer (aprs_service.c).
    aprs_modem_config_t modem_cfg = {
        .adc_pin = g_config.adc_gpio,
        .dac_pin = g_config.dac_gpio,
        .ptt_pin = g_config.rf_ptt_gpio,
        .sql_pin = g_config.rf_sql_gpio,
        .pwr_pin = g_config.rf_pwr_gpio,
        .ptt_active = g_config.rf_ptt_active,
        .sql_active = g_config.rf_sql_active,
        .pwr_active = g_config.rf_pwr_active,
        .adc_atten = g_config.adc_atten,
        // Audio ADC/DAC AFSK modulation (300/1200/1200 V.23/9600 Bd), set on the
        // Radio / Modem (Audio / AFSK) webconfig page. This is intentionally NOT
        // g_config.modem_type - that field holds the *optional RF module's* modem
        // mode (RF_MODE_OFF/LoRa/G3RUH/GFSK/DPRS), a completely different value
        // space, and feeding it to afskSetModem() here would silently corrupt the
        // AFSK modem configuration whenever the RF module mode was changed.
        .modem_type = g_config.afsk_modem_type,
        .bpf = g_config.audio_lpf,
        .tx_timeslot = g_config.tx_timeslot,
        .preamble = g_config.preamble,
        .fx25_mode = g_config.fx25_mode,
    };
    // Only bring up the audio ADC/DAC AFSK modem hardware when it's enabled
    // on the Radio / Modem (Audio / AFSK) webconfig page.
    if (g_config.audio_modem_en) {
        APRS_init(&modem_cfg);
        aprs_service_notify_modem_ready();
    } else {
        ESP_LOGI(TAG, "Audio ADC/DAC AFSK modem disabled in config - skipping APRS_init()");
    }
    APRS_setCallsign(g_config.aprs_mycall, g_config.aprs_ssid);
    aprs_service_start();

    ESP_LOGI(TAG, "ESP32APRS web admin ready. Login: %s / %s", g_config.http_username, g_config.http_password);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
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
