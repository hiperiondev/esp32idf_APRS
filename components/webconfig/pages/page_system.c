/**
 * @file page_system.c
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
 * @brief Web admin "System" page: renders and saves the system configuration
 * (credentials, time sync and NTP hosts, timezone), the chip/CPU frequency
 * info and control (formerly on the separate System Information page), and
 * implements the reset-to-defaults action.
 */

#include <stdio.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_rom_sys.h"

#include "app_config.h"
#include "cpu_freq.h"
#include "pages.h"
#include "translations.h"
#include "web_common.h"

esp_err_t page_system_get(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    web_send_header(req, TR_F_SYSTEM, "system");

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    uint32_t cpu_mhz = esp_rom_get_cpu_ticks_per_us();

    char buf[3200];
    snprintf(buf, sizeof(buf),
             "<form method='POST' action='/system'>"
             "<fieldset><legend>" TR_SYSINFO_CHIP "</legend>"
             "<p><b>" TR_SYSINFO_MODEL "</b> %d &nbsp; <b>" TR_SYSINFO_CORES "</b> %d &nbsp; <b>" TR_SYSINFO_REVISION "</b> %d</p>"
             "<p><b>" TR_SYSINFO_CPU_FREQ "</b> %lu MHz</p>"
             "<label>" TR_SYSINFO_CPU_FREQ_SET "</label>"
             "<select name='cpuFreq'>"
             "<option value='80' %s>80</option><option value='160' %s>160</option><option value='240' %s>240</option>"
             "</select>"
             "<p><small>" TR_SYSINFO_CPU_FREQ_NOTE "</small></p>"
             "<p><b>" TR_SYSINFO_FLASH_SIZE "</b> %lu bytes</p></fieldset>"
             "<fieldset><legend>" TR_SYS_WEB_ADMIN_LOGIN "</legend>"
             "<label>" TR_F_USERNAME "</label><input type='text' name='httpUser' value='%s' maxlength='31'>"
             "<label>" TR_F_PASSWORD "</label><input type='password' name='httpPass' id='pwd_httpPass' value='%s' maxlength='63'>"
             "<label class='pwd-show'><input type='checkbox' onclick=\"togglePwd('pwd_httpPass',this)\"> " TR_SHOW_PASSWORD "</label>"
             "</fieldset>"
             "<fieldset><legend>" TR_SYS_DEVICE "</legend>"
             "<label>" TR_SYS_HOST_NAME "</label><input type='text' name='hostName' value='%s' maxlength='31'>"
             "<label>" TR_SYS_TIME_ZONE "</label><input type='number' step='0.5' name='timeZone' value='%.1f'>"
             "<label><input type='checkbox' name='syncTime' %s> " TR_SYS_SYNC_NTP "</label>"
             "<label>" TR_SYS_NTP_HOST "</label><input type='text' name='ntpHost0' value='%s' maxlength='19'>"
             "<label>" TR_SYS_NTP_HOST2 "</label><input type='text' name='ntpHost1' value='%s' maxlength='19'>"
             "<label>" TR_SYS_NTP_HOST3 "</label><input type='text' name='ntpHost2' value='%s' maxlength='19'>"
             "<label>" TR_SYS_NTP_RESYNC "</label><input type='number' name='ntpResync' value='%d' min='30'>"
             "<label>" TR_SYS_AUTO_RESET_TIMEOUT "</label><input type='number' name='resetTimeout' value='%d'>"
             "</fieldset>"
             "<button type='submit'>" TR_BTN_SAVE "</button></form>"
             "<form method='POST' action='/default' onsubmit=\"return confirm('" TR_SYS_CONFIRM_FACTORY_RESET "');\">"
             "<button class='danger' type='submit'>" TR_SYS_FACTORY_RESET "</button></form>",
             (int)chip.model, (int)chip.cores, (int)chip.revision, (unsigned long)cpu_mhz,
             g_config.cpuFreq == 80 ? "selected" : "", g_config.cpuFreq == 160 ? "selected" : "", g_config.cpuFreq == 240 ? "selected" : "",
             (unsigned long)flash_size,
             g_config.http_username, g_config.http_password, g_config.host_name, g_config.timeZone, g_config.synctime ? "checked" : "",
             g_config.ntp_host[0], g_config.ntp_host[1], g_config.ntp_host[2], g_config.ntp_resync_sec,
             g_config.reset_timeout);
    httpd_resp_sendstr_chunk(req, buf);
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
    web_form_get(body, "hostName", g_config.host_name, sizeof(g_config.host_name));
    g_config.timeZone = web_form_get_float(body, "timeZone", g_config.timeZone);
    g_config.synctime = web_form_get_bool(body, "syncTime");
    web_form_get(body, "ntpHost0", g_config.ntp_host[0], sizeof(g_config.ntp_host[0]));
    web_form_get(body, "ntpHost1", g_config.ntp_host[1], sizeof(g_config.ntp_host[1]));
    web_form_get(body, "ntpHost2", g_config.ntp_host[2], sizeof(g_config.ntp_host[2]));
    g_config.ntp_resync_sec = (uint16_t)web_form_get_int(body, "ntpResync", g_config.ntp_resync_sec);
    if (g_config.ntp_resync_sec < NTP_RESYNC_MIN_SEC)
        g_config.ntp_resync_sec = NTP_RESYNC_MIN_SEC;
    g_config.reset_timeout = (uint16_t)web_form_get_int(body, "resetTimeout", g_config.reset_timeout);

    int freq = web_form_get_int(body, "cpuFreq", g_config.cpuFreq);
    if (freq == 80 || freq == 160 || freq == 240)
        g_config.cpuFreq = (uint8_t)freq;

    app_config_unlock();

    app_config_save();
    // Apply the CPU frequency immediately (mirrors previous /sysinfo
    // behavior); main.c also calls cpu_freq_apply() right after
    // app_config_load() at boot, so this selection is re-applied on every
    // subsequent power-up too.
    cpu_freq_apply();
    web_send_saved_redirect(req, "/system");
    return ESP_OK;
}

esp_err_t page_default_reset(httpd_req_t *req) {
    if (!web_check_auth(req))
        return ESP_OK;
    app_config_factory_reset();
    web_send_saved_redirect(req, "/system");
    return ESP_OK;
}
