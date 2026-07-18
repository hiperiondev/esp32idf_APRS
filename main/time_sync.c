/**
 * @file time_sync.c
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
 * @brief SNTP client task: waits for real internet connectivity, registers every
 * configured NTP host at once, and keeps the system clock synchronized in UTC,
 * retrying until the first sync succeeds.
 */

#include <sys/time.h>
#include <time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "net_state.h"
#include "time_sync.h"

static const char *TAG = "time_sync";

static void logUtcNow(const char *prefix) {
    time_t now = time(NULL);
    struct tm tmv;
    gmtime_r(&now, &tmv);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    ESP_LOGI(TAG, "%s: %s UTC", prefix, buf);
}

static void time_sync_notification_cb(struct timeval *tv) {
    logUtcNow("SNTP time synced");
}

// Runs once from time_sync_start(). Waits for real internet connectivity
// (same net_state gate the IGate uses - see net_state.h), then explicitly
// requests a sync and *waits* for the result so failures are logged clearly
// instead of silently never happening. Retries every 30 s until the first
// sync succeeds; esp_netif_sntp's own internal periodic mode keeps the clock
// in sync afterwards (default ~1 h resync interval).
static void timeSyncTask(void *arg) {
    while (!net_state_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    setenv("TZ", "UTC0", 1);
    tzset();

    // Build the list of non-empty configured hosts (falling back to
    // pool.ntp.org if the user has somehow cleared all 3 fields). esp_netif's
    // SNTP wrapper tries each configured server (round-robin) on its own, so
    // handing it all 3 up front gives automatic fallback if one is
    // unreachable/blocked, instead of only ever retrying a single host.
    const char *hosts[NTP_HOST_NUM];
    int hostCount = 0;
    for (int i = 0; i < NTP_HOST_NUM; i++) {
        if (g_config.ntp_host[i][0]) {
            hosts[hostCount++] = g_config.ntp_host[i];
        }
    }
    if (hostCount == 0) {
        hosts[0] = "pool.ntp.org";
        hostCount = 1;
    }

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(hosts[0]);
    config.num_of_servers = hostCount;
    for (int i = 0; i < hostCount; i++) {
        config.servers[i] = hosts[i];
    }
    config.sync_cb = time_sync_notification_cb;
    config.start = false; // we call esp_netif_sntp_start() ourselves below so we can wait + retry + log
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_sntp_init() failed: %s - NTP sync will not run", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    // Re-check against the NTP server at the user-configured interval
    // (System page "NTP resync interval", enforced minimum NTP_RESYNC_MIN_SEC
    // = 30 s). Note: lwip's sntp module enforces a 15 s floor
    // (SNTP_UPDATE_DELAY) regardless of what's requested here, so an
    // effective interval below 15 s would be silently clamped anyway - see
    // the note in time_sync.h.
    uint32_t resyncSec = g_config.ntp_resync_sec;
    if (resyncSec < NTP_RESYNC_MIN_SEC)
        resyncSec = NTP_RESYNC_MIN_SEC;
    sntp_set_sync_interval(resyncSec * 1000);

    // Human-readable list of configured hosts, for logging only.
    char hostList[3 * sizeof(g_config.ntp_host[0]) + 8] = { 0 };
    int hostListLen = 0;
    for (int i = 0; i < hostCount; i++) {
        hostListLen += snprintf(&hostList[hostListLen], sizeof(hostList) - hostListLen, "%s%s", hosts[i], (i + 1 < hostCount) ? ", " : "");
    }

    for (;;) {
        err = esp_netif_sntp_start();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_netif_sntp_start() failed: %s, retrying in 30 s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(30000));
            continue;
        }

        ESP_LOGI(TAG, "NTP sync requested against '%s', waiting up to 25 s for a reply...", hostList);
        err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(25000));
        if (err == ESP_OK) {
            logUtcNow("System clock set from NTP");
            break; // esp_netif_sntp keeps itself in sync from here on its own periodic timer
        }

        ESP_LOGW(TAG,
                 "NTP sync did not complete in time (%s). Check that one of '%s' resolves and that outbound UDP/123 "
                 "is allowed on this network. Retrying in 30 s...",
                 esp_err_to_name(err), hostList);
        vTaskDelay(pdMS_TO_TICKS(30000));
    }

    vTaskDelete(NULL);
}

void time_sync_start(void) {
    if (!g_config.synctime) {
        ESP_LOGI(TAG, "NTP time sync disabled (System page \"Sync Time\" checkbox is off)");
        return;
    }
    xTaskCreate(timeSyncTask, "time_sync", 4096, NULL, 4, NULL);
}
