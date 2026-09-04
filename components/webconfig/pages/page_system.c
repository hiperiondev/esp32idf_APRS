// @file page_system.c
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
// @brief Web admin "System" page: renders and saves the system configuration
// (credentials, time sync and NTP hosts), the chip and CPU frequency
// information and control, and the reset-to-defaults action.

#include <stdio.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_rom_sys.h"

#include "app_config.h"
#include "cpu_freq.h"
#include "esp_log.h"
#include "pages.h"
#include "time_sync.h" // time_sync_tz_count() / time_sync_tz_name() - "Time" section timezone select
#include "translations.h"
#include "web_common.h"

static const char *TAG = "page_system";

esp_err_t page_system_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_SYSTEM, "system");
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/system'>");

    // CHIP -----------------------------------------------------------------
    // Fixed-text markup plus a handful of small, individually-sized fields:
    // each snprintf below only ever holds one line of this fieldset, so the
    // peak stack usage of this handler stays well under the request body
    // buffers used elsewhere on this page.
    web_fieldset_open(req, TR_SYSINFO_CHIP);
    {
        esp_chip_info_t chip;
        esp_chip_info(&chip);
        uint32_t cpu_mhz = esp_rom_get_cpu_ticks_per_us();
        char buf[160];
        snprintf(buf, sizeof(buf), "<p><b>" TR_SYSINFO_MODEL "</b> %d &nbsp; <b>" TR_SYSINFO_CORES "</b> %d &nbsp; <b>" TR_SYSINFO_REVISION "</b> %d</p>",
                 (int)chip.model, (int)chip.cores, (int)chip.revision);
        httpd_resp_sendstr_chunk(req, buf);
        snprintf(buf, sizeof(buf), "<p><b>" TR_SYSINFO_CPU_FREQ "</b> %lu MHz</p>", (unsigned long)cpu_mhz);
        httpd_resp_sendstr_chunk(req, buf);
    }
    // CPU frequency selector: a fixed 3-entry table (80/160/240 MHz), so it is
    // rendered with the shared <select> helpers rather than a hand-rolled
    // three-way ternary chain.
    web_select_open(req, TR_SYSINFO_CPU_FREQ_SET, "cpuFreq");
    web_select_option(req, 80, "80", g_config.cpuFreq == 80);
    web_select_option(req, 160, "160", g_config.cpuFreq == 160);
    web_select_option(req, 240, "240", g_config.cpuFreq == 240);
    web_select_close(req);
    httpd_resp_sendstr_chunk(req, "<p><small>" TR_SYSINFO_CPU_FREQ_NOTE "</small></p>");
    {
        uint32_t flash_size = 0;
        esp_flash_get_size(NULL, &flash_size);
        char buf[80];
        snprintf(buf, sizeof(buf), "<p><b>" TR_SYSINFO_FLASH_SIZE "</b> %lu bytes</p>", (unsigned long)flash_size);
        httpd_resp_sendstr_chunk(req, buf);
    }
    web_fieldset_close(req);

    // WEB ADMIN LOGIN --------------------------------------------------------
    // web_field_text() escapes and bounds the value in its own small internal
    // buffer, so neither field needs a caller-side escape buffer here.
    web_fieldset_open(req, TR_SYS_WEB_ADMIN_LOGIN);
    web_field_text(req, TR_F_USERNAME, "httpUser", g_config.http_username, 31);
    {
        char buf[600];
        char esc_pass[64 * 6 + 1];
        web_html_attr_escape(g_config.http_password, esc_pass, sizeof(esc_pass));
        snprintf(buf, sizeof(buf), "<label>" TR_F_PASSWORD "</label><input type='password' name='httpPass' id='pwd_httpPass' value='%s' maxlength='63'>",
                 esc_pass);
        httpd_resp_sendstr_chunk(req, buf);
    }
    httpd_resp_sendstr_chunk(req, "<label class='pwd-show'><input type='checkbox' onclick=\"togglePwd('pwd_httpPass',this)\"> " TR_SHOW_PASSWORD "</label>"
                                  "<p><small>" TR_SYS_WEB_ADMIN_LOGIN_NOTE "</small></p>");
    web_fieldset_close(req);

    // TIME -------------------------------------------------------------------
    web_fieldset_open(req, TR_SYS_TIME);
    web_field_checkbox(req, TR_SYS_SYNC_NTP, "syncTime", g_config.synctime);
    web_field_text(req, TR_SYS_NTP_HOST, "ntpHost0", g_config.ntp_host[0], 19);
    web_field_text(req, TR_SYS_NTP_HOST2, "ntpHost1", g_config.ntp_host[1], 19);
    web_field_text(req, TR_SYS_NTP_HOST3, "ntpHost2", g_config.ntp_host[2], 19);
    // No upper bound on this field: the resync interval only ever needs a
    // floor (::NTP_RESYNC_MIN_SEC, enforced again on save), so it is rendered
    // as a plain min-only number input rather than through web_field_int(),
    // which always emits both a min and a max attribute.
    {
        char buf[120];
        snprintf(buf, sizeof(buf), "<label>" TR_SYS_NTP_RESYNC "</label><input type='number' name='ntpResync' value='%d' min='30'>", g_config.ntp_resync_sec);
        httpd_resp_sendstr_chunk(req, buf);
    }

    // Time zone select, appended at the end of the "Time" section: this only
    // affects how the dashboard renders local date/time (see page_common.c
    // page_dashinfo()) - the system clock itself and every APRS timestamp
    // stay UTC regardless of this selection (see time_sync.h).
    web_select_open(req, TR_SYS_TIMEZONE, "timeZone");
    uint8_t tzCount = time_sync_tz_count();
    for (uint8_t i = 0; i < tzCount; i++)
        web_select_option(req, i, time_sync_tz_name(i), i == g_config.timezone_idx);
    web_select_close(req);
    web_fieldset_close(req);

    httpd_resp_sendstr_chunk(req, "<button type='submit'>" TR_BTN_SAVE "</button></form>"
                                  "<form method='POST' action='/default' onsubmit=\"return confirm('" TR_SYS_CONFIRM_FACTORY_RESET "');\">"
                                  "<button class='danger' type='submit'>" TR_SYS_FACTORY_RESET "</button></form>");
    web_send_footer(req);
    return ESP_OK;
}

esp_err_t page_system_post(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    char body[1200];
    if (web_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    app_config_lock();
    web_form_get(body, "httpUser", g_config.http_username, sizeof(g_config.http_username));
    web_form_get(body, "httpPass", g_config.http_password, sizeof(g_config.http_password));
    g_config.synctime = web_form_get_bool(body, "syncTime");
    web_form_get(body, "ntpHost0", g_config.ntp_host[0], sizeof(g_config.ntp_host[0]));
    web_form_get(body, "ntpHost1", g_config.ntp_host[1], sizeof(g_config.ntp_host[1]));
    web_form_get(body, "ntpHost2", g_config.ntp_host[2], sizeof(g_config.ntp_host[2]));
    g_config.ntp_resync_sec = (uint16_t)web_form_get_int(body, "ntpResync", g_config.ntp_resync_sec);
    if (g_config.ntp_resync_sec < NTP_RESYNC_MIN_SEC)
        g_config.ntp_resync_sec = NTP_RESYNC_MIN_SEC;

    int tz = web_form_get_int(body, "timeZone", g_config.timezone_idx);
    if (tz < 0 || tz >= time_sync_tz_count())
        tz = 0;
    g_config.timezone_idx = (uint8_t)tz;

    int freq = web_form_get_int(body, "cpuFreq", g_config.cpuFreq);
    if (freq == 80 || freq == 160 || freq == 240)
        g_config.cpuFreq = (uint8_t)freq;

    app_config_unlock();

    // The page rendered next is built from the live settings, so the save
    // result is what decides whether the operator is told this reached flash.
    bool ok = app_config_save();
    if (!ok)
        ESP_LOGE(TAG, "system settings could not be written to flash");

    // Apply the CPU frequency immediately; main.c also calls cpu_freq_apply()
    // right after app_config_load() at boot, so this selection is re-applied
    // on every subsequent power-up too.
    cpu_freq_apply();
    web_send_save_result(req, ok, "/system");
    return ESP_OK;
}

esp_err_t page_default_reset(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    // Same reasoning as the save handler: the defaults are live in RAM the
    // moment this returns, so only the result tells the two cases apart.
    bool ok = app_config_factory_reset();
    if (!ok)
        ESP_LOGE(TAG, "factory defaults could not be written to flash");
    web_send_save_result(req, ok, "/system");
    return ESP_OK;
}
