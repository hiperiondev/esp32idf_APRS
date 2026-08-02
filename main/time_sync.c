// @file time_sync.c
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
// @brief SNTP client task: waits for real internet connectivity, registers every
// configured NTP host at once, and keeps the system clock synchronized in UTC,
// retrying until the first sync succeeds.

#include <string.h>
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
#include "str_append.h"
#include "time_sync.h"

static const char *TAG = "time_sync";

// SNTP bootstrap state machine, driven one step per second by time_sync_1hz()
// from the APRS service's existing 1 Hz tick (serviceTickTask in
// aprs_service.c) instead of from a dedicated 4 KB-stack task. It only needs to
// get the FIRST sync: once the clock is set, esp_netif_sntp's own internal
// periodic timer keeps it in sync, so the state machine parks in TS_DONE and
// does nothing further.
typedef enum {
    TS_DISABLED = 0, // "Sync Time" off, or init failed: never do anything
    TS_WAIT_NET,     // waiting for real internet connectivity (net_state gate)
    TS_START,        // (re)request a sync via esp_netif_sntp_start()
    TS_WAITING,      // sync requested; polling s_synced, with a 25 s per-attempt budget
    TS_COOLDOWN,     // last attempt failed; wait 30 s before retrying
    TS_DONE,         // first sync done; esp_netif_sntp self-maintains from here
} ts_state_t;

static ts_state_t s_state = TS_DISABLED;
static volatile bool s_synced = false;                         // set by the SNTP notification callback (tcpip thread)
static bool s_sntp_inited = false;                             // esp_netif_sntp_init() done exactly once
static int s_wait_s = 0;                                       // seconds elapsed in TS_WAITING / TS_COOLDOWN
static char s_host_list[3 * sizeof(g_config.ntp_host[0]) + 8]; // human-readable, for logs

// Module-owned copies of the configured NTP hostnames. esp_netif_sntp_init()
// keeps the pointers it is handed and lwIP's SNTP module dereferences them
// asynchronously, on its own timer, for as long as the client runs - so they
// must not point into g_config, whose strings a System-page save rewrites in
// place underneath it. These copies are taken once, under the config lock, and
// never change afterwards.
static char s_ntp_host[NTP_HOST_NUM][sizeof(g_config.ntp_host[0])];

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
    // Observed by the 1 Hz tick (TS_WAITING) so it can move to TS_DONE without
    // ever blocking on esp_netif_sntp_sync_wait(). Harmless when set again by
    // the periodic re-syncs after TS_DONE (ignored in that state).
    s_synced = true;
}

// One-time SNTP initialization, run the first time the device has real internet
// connectivity. Registers every non-empty configured host at once (falling back
// to pool.ntp.org if the user cleared all 3 fields); esp_netif's SNTP wrapper
// round-robins them on its own, giving automatic fallback if one is
// unreachable/blocked. config.start=false so time_sync_1hz() drives start/retry
// itself (non-blocking) instead of a task blocking on esp_netif_sntp_sync_wait().
// Returns false if init fails (caller then parks in TS_DISABLED).
static bool sntp_setup(void) {
    setenv("TZ", "UTC0", 1);
    tzset();

    // Snapshot the configured hosts into this module's own storage, under the
    // config lock, and hand esp_netif_sntp_init() pointers to those copies -
    // see the s_ntp_host note above. The lock is a strict leaf lock, held only
    // for the copy.
    const char *hosts[NTP_HOST_NUM];
    int hostCount = 0;
    app_config_lock();
    for (int i = 0; i < NTP_HOST_NUM; i++) {
        if (g_config.ntp_host[i][0]) {
            strncpy(s_ntp_host[hostCount], g_config.ntp_host[i], sizeof(s_ntp_host[hostCount]) - 1);
            s_ntp_host[hostCount][sizeof(s_ntp_host[hostCount]) - 1] = 0;
            hosts[hostCount] = s_ntp_host[hostCount];
            hostCount++;
        }
    }
    uint32_t resyncSec = g_config.ntp_resync_sec;
    app_config_unlock();

    if (hostCount == 0) {
        // A string literal has static storage duration, so it is as safe to
        // hand over as the copies above.
        hosts[0] = "pool.ntp.org";
        hostCount = 1;
    }

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(hosts[0]);
    config.num_of_servers = hostCount;
    for (int i = 0; i < hostCount; i++) {
        config.servers[i] = hosts[i];
    }
    config.sync_cb = time_sync_notification_cb;
    config.start = false; // time_sync_1hz() calls esp_netif_sntp_start() so it can retry + log
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_sntp_init() failed: %s - NTP sync will not run", esp_err_to_name(err));
        return false;
    }

    // Re-check against the NTP server at the user-configured interval
    // (System page "NTP resync interval", enforced minimum NTP_RESYNC_MIN_SEC
    // = 30 s). Note: lwip's sntp module enforces a 15 s floor
    // (SNTP_UPDATE_DELAY) regardless of what's requested here, so an
    // effective interval below 15 s would be silently clamped anyway - see
    // the note in time_sync.h.
    if (resyncSec < NTP_RESYNC_MIN_SEC)
        resyncSec = NTP_RESYNC_MIN_SEC;
    sntp_set_sync_interval(resyncSec * 1000);

    // Human-readable list of configured hosts, for logging only. Built with
    // str_append() so the running offset stays clamped: the buffer is sized
    // for NTP_HOST_NUM full-width hostnames plus separators, but that sizing
    // is an assumption about the config field widths rather than something
    // this loop can enforce, and the only cost of getting it wrong should be
    // a shortened log line.
    size_t n = 0;
    s_host_list[0] = 0;
    for (int i = 0; i < hostCount; i++) {
        str_append(s_host_list, sizeof(s_host_list), &n, "%s%s", hosts[i], (i + 1 < hostCount) ? ", " : "");
    }
    return true;
}

// One step of the SNTP bootstrap, called once per second from the APRS service's
// 1 Hz tick (serviceTickTask). Non-blocking: it only ever advances the state
// machine and returns, so it never stalls that shared tick. The whole bootstrap
// therefore costs no task and no stack of its own - the per-second connectivity
// poll and the 25 s/30 s wait+retry budgets map naturally onto 1 Hz ticks. The
// sequence is: wait for connectivity, request a sync, retry every 30 s until the
// first one lands, then let esp_netif_sntp self-maintain.
void time_sync_1hz(void) {
    switch (s_state) {
        case TS_DISABLED:
        case TS_DONE:
            return;

        case TS_WAIT_NET:
            // Same net_state gate the IGate uses (see net_state.h); one tick is
            // one second of polling.
            if (!net_state_is_connected())
                return;
            if (!s_sntp_inited) {
                if (!sntp_setup()) {
                    s_state = TS_DISABLED; // init failed: give up for this boot
                    return;
                }
                s_sntp_inited = true;
            }
            s_state = TS_START;
            // fall through: kick the first request on this same tick
            __attribute__((fallthrough));

        case TS_START: {
            s_synced = false;
            esp_err_t err = esp_netif_sntp_start();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "esp_netif_sntp_start() failed: %s, retrying in 30 s", esp_err_to_name(err));
                s_wait_s = 0;
                s_state = TS_COOLDOWN;
                return;
            }
            ESP_LOGI(TAG, "NTP sync requested against '%s', waiting up to 25 s for a reply...", s_host_list);
            s_wait_s = 0;
            s_state = TS_WAITING;
            return;
        }

        case TS_WAITING:
            if (s_synced) {
                logUtcNow("System clock set from NTP");
                s_state = TS_DONE; // esp_netif_sntp keeps itself in sync from here on its own periodic timer
                return;
            }
            if (++s_wait_s >= 25) {
                ESP_LOGW(TAG,
                         "NTP sync did not complete in time. Check that one of '%s' resolves and that outbound UDP/123 "
                         "is allowed on this network. Retrying in 30 s...",
                         s_host_list);
                s_wait_s = 0;
                s_state = TS_COOLDOWN;
            }
            return;

        case TS_COOLDOWN:
            if (++s_wait_s >= 30)
                s_state = TS_START;
            return;
    }
}

void time_sync_start(void) {
    if (!g_config.synctime) {
        ESP_LOGI(TAG, "NTP time sync disabled (System page \"Sync Time\" checkbox is off)");
        s_state = TS_DISABLED;
        return;
    }
    // No dedicated task: arm the state machine and let the APRS service's 1 Hz
    // tick drive it via time_sync_1hz() (saves that task's 4 KB stack). Safe to
    // call before the tick task exists - until then the machine simply idles in
    // TS_WAIT_NET.
    s_state = TS_WAIT_NET;
}
