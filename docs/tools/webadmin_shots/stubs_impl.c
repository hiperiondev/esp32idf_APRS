// Host stand-ins for the ESP-IDF runtime and for the services the web admin
// pages query, so the real page handlers can render on a PC. The values they
// report are those of a plausible running station, not live measurements.
#include "afsk.h"
#include "app_config.h"
#include "aprs_service.h"
#include "bmp280.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gps.h"
#include "heap_monitor.h"
#include "igate.h"
#include "message.h"
#include "storage.h"
#include "telegram_app.h"
#include "winlink.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ---------------------------------------------------------------- capture
char *g_out;
size_t g_out_len, g_out_cap;
char g_ctype[64];
char g_query[512];
char g_body[8192];
size_t g_body_len, g_body_pos;
static void out_append(const char *b, size_t n) {
    if (g_out_len + n + 1 > g_out_cap) {
        g_out_cap = (g_out_len + n + 1) * 2;
        g_out = realloc(g_out, g_out_cap);
    }
    memcpy(g_out + g_out_len, b, n);
    g_out_len += n;
    g_out[g_out_len] = 0;
}
esp_err_t httpd_resp_send(httpd_req_t *r, const char *b, ssize_t l) {
    if (b)
        out_append(b, l < 0 ? strlen(b) : (size_t)l);
    return 0;
}
esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *s) {
    if (s)
        out_append(s, strlen(s));
    return 0;
}
esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *b, ssize_t l) {
    if (b)
        out_append(b, l < 0 ? strlen(b) : (size_t)l);
    return 0;
}
esp_err_t httpd_resp_sendstr_chunk(httpd_req_t *r, const char *s) {
    if (s)
        out_append(s, strlen(s));
    return 0;
}
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *t) {
    snprintf(g_ctype, sizeof g_ctype, "%s", t);
    return 0;
}
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *f, const char *v) {
    return 0;
}
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *s) {
    return 0;
}
esp_err_t httpd_resp_send_500(httpd_req_t *r) {
    out_append("500", 3);
    return 0;
}
esp_err_t httpd_resp_send_404(httpd_req_t *r) {
    out_append("404", 3);
    return 0;
}
esp_err_t httpd_resp_send_err(httpd_req_t *r, int c, const char *m) {
    if (m)
        out_append(m, strlen(m));
    return 0;
}
int httpd_req_recv(httpd_req_t *r, char *buf, size_t len) {
    size_t n = g_body_len - g_body_pos;
    if (n > len)
        n = len;
    if (!n)
        return 0;
    memcpy(buf, g_body + g_body_pos, n);
    g_body_pos += n;
    return (int)n;
}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *f, char *v, size_t l) {
    const char *val = NULL;
    if (!strcasecmp(f, "Authorization"))
        val = "Basic YWRtaW46YWRtaW4=";
    else if (!strcasecmp(f, "Host"))
        val = "192.168.4.1";
    else if (!strcasecmp(f, "Origin"))
        val = "http://192.168.4.1";
    else if (!strcasecmp(f, "Accept-Language"))
        val = "en";
    if (!val)
        return ESP_ERR_NOT_FOUND;
    snprintf(v, l, "%s", val);
    return 0;
}
size_t httpd_req_get_hdr_value_len(httpd_req_t *r, const char *f) {
    char b[128];
    return httpd_req_get_hdr_value_str(r, f, b, sizeof b) == 0 ? strlen(b) : 0;
}
size_t httpd_req_get_url_query_len(httpd_req_t *r) {
    return strlen(g_query);
}
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r, char *buf, size_t l) {
    if (!g_query[0])
        return ESP_ERR_NOT_FOUND;
    snprintf(buf, l, "%s", g_query);
    return 0;
}
esp_err_t httpd_query_key_value(const char *q, const char *k, char *v, size_t l) {
    size_t kl = strlen(k);
    const char *p = q;
    while (p && *p) {
        if (!strncmp(p, k, kl) && p[kl] == '=') {
            p += kl + 1;
            size_t i = 0;
            while (*p && *p != '&' && i + 1 < l)
                v[i++] = *p++;
            v[i] = 0;
            return 0;
        }
        p = strchr(p, '&');
        if (p)
            p++;
    }
    return ESP_ERR_NOT_FOUND;
}
int httpd_req_to_sockfd(httpd_req_t *r) {
    return -1;
}
esp_err_t httpd_start(httpd_handle_t *h, const httpd_config_t *c) {
    return 0;
}
esp_err_t httpd_register_uri_handler(httpd_handle_t h, const httpd_uri_t *u) {
    return 0;
}

// ---------------------------------------------------------------- IDF core
const char *esp_err_to_name(esp_err_t e) {
    return e == 0 ? "ESP_OK" : "ESP_FAIL";
}
vprintf_like_t esp_log_set_vprintf(vprintf_like_t f) {
    return f;
}
static int64_t s_t0;
int64_t esp_timer_get_time(void) {
    return 3LL * 86400 * 1000000 + 4 * 3600LL * 1000000 + 17 * 60LL * 1000000 + 42LL * 1000000;
}
esp_err_t esp_timer_create(const esp_timer_create_args_t *a, esp_timer_handle_t *h) {
    *h = (void *)1;
    return 0;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t h, uint64_t us) {
    return 0;
}
esp_err_t esp_timer_start_periodic(esp_timer_handle_t h, uint64_t us) {
    return 0;
}
esp_err_t esp_timer_stop(esp_timer_handle_t h) {
    return 0;
}
esp_reset_reason_t esp_reset_reason(void) {
    return ESP_RST_POWERON;
}
void esp_restart(void) {
}
uint32_t esp_get_free_heap_size(void) {
    return 61234;
}
uint32_t esp_get_minimum_free_heap_size(void) {
    return 38120;
}
size_t heap_caps_get_free_size(uint32_t c) {
    return 61234;
}
size_t heap_caps_get_minimum_free_size(uint32_t c) {
    return 38120;
}
size_t heap_caps_get_largest_free_block(uint32_t c) {
    return 30708;
}
size_t heap_caps_get_total_size(uint32_t c) {
    return 290000;
}
void esp_chip_info(esp_chip_info_t *i) {
    i->model = CHIP_ESP32;
    i->features = CHIP_FEATURE_WIFI_BGN | CHIP_FEATURE_BT | CHIP_FEATURE_BLE;
    i->revision = 301;
    i->cores = 2;
}
esp_flash_t *esp_flash_default_chip;
esp_err_t esp_flash_get_size(esp_flash_t *c, uint32_t *s) {
    *s = 4 * 1024 * 1024;
    return 0;
}
uint32_t esp_rom_get_cpu_ticks_per_us(void) {
    return 240;
}
static esp_app_desc_t s_desc = { .version = "1.0.0", .project_name = "esp32idf_APRS", .time = "09:00:00", .date = "Nov 15 2026", .idf_ver = "v6.1" };
const esp_app_desc_t *esp_app_get_description(void) {
    return &s_desc;
}
static esp_partition_t s_p0 = { 0, 0x10, 0x20000, 0x1B0000, "ota_0" }, s_p1 = { 0, 0x11, 0x1D0000, 0x1B0000, "ota_1" };
const esp_partition_t *esp_ota_get_running_partition(void) {
    return &s_p0;
}
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *s) {
    return &s_p1;
}
const esp_partition_t *esp_ota_get_boot_partition(void) {
    return &s_p0;
}
esp_err_t esp_ota_begin(const esp_partition_t *p, size_t s, esp_ota_handle_t *h) {
    return ESP_FAIL;
}
esp_err_t esp_ota_write(esp_ota_handle_t h, const void *d, size_t s) {
    return ESP_FAIL;
}
esp_err_t esp_ota_end(esp_ota_handle_t h) {
    return ESP_FAIL;
}
esp_err_t esp_ota_abort(esp_ota_handle_t h) {
    return 0;
}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p) {
    return ESP_FAIL;
}
esp_err_t esp_wifi_set_mode(wifi_mode_t m) {
    return 0;
}
esp_err_t esp_wifi_get_mode(wifi_mode_t *m) {
    *m = WIFI_MODE_APSTA;
    return 0;
}
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *r) {
    memset(r, 0, sizeof *r);
    strcpy((char *)r->ssid, "HomeNet");
    r->rssi = -58;
    r->primary = 6;
    return 0;
}
esp_err_t esp_wifi_set_config(wifi_interface_t i, wifi_config_t *c) {
    return 0;
}
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *c, bool b) {
    return 0;
}
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *n) {
    *n = 0;
    return 0;
}
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *n, wifi_ap_record_t *r) {
    *n = 0;
    return 0;
}
esp_err_t esp_wifi_clear_ap_list(void) {
    return 0;
}
esp_err_t esp_wifi_connect(void) {
    return 0;
}
// FreeRTOS
BaseType_t xPortInIsrContext(void) {
    return 0;
}
TickType_t xTaskGetTickCount(void) {
    return 1000;
}
void vTaskDelay(TickType_t t) {
}
BaseType_t xTaskCreate(void (*f)(void *), const char *n, uint32_t s, void *p, UBaseType_t pr, TaskHandle_t *h) {
    return pdPASS;
}
TaskHandle_t xTaskGetCurrentTaskHandle(void) {
    return (void *)1;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return (void *)1;
}
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) {
    return (void *)1;
}
SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    return (void *)1;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t t) {
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t s) {
    return pdTRUE;
}
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t s, TickType_t t) {
    return pdTRUE;
}
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t s) {
    return pdTRUE;
}
TaskHandle_t xSemaphoreGetMutexHolder(SemaphoreHandle_t s) {
    return NULL;
}
void vSemaphoreDelete(SemaphoreHandle_t s) {
}
void configASSERT(int x) {
}
// BME280 behind the esp-idf-lib API
esp_err_t i2cdev_init(void) {
    return 0;
}
esp_err_t bmp280_init_desc(bmp280_t *d, uint8_t a, int p, gpio_num_t sda, gpio_num_t scl) {
    d->id = BME280_CHIP_ID;
    return 0;
}
esp_err_t bmp280_free_desc(bmp280_t *d) {
    return 0;
}
esp_err_t bmp280_init_default_params(bmp280_params_t *p) {
    memset(p, 0, sizeof *p);
    return 0;
}
esp_err_t bmp280_init(bmp280_t *d, bmp280_params_t *p) {
    d->id = BME280_CHIP_ID;
    return 0;
}
esp_err_t bmp280_read_float(bmp280_t *d, float *t, float *p, float *h) {
    *t = 18.4f;
    *p = 101325.0f;
    if (h)
        *h = 63.0f;
    return 0;
}

// ---------------------------------------------------------------- services
bool afskCaptureRead(afsk_capture_block_t *o) {
    return false;
}
esp_err_t afskCaptureStart(uint32_t s) {
    return ESP_FAIL;
}
uint32_t afskCaptureStop(void) {
    return 0;
}
bool afsk_gpio_is_output_capable(int8_t g) {
    return g >= 0 && g < 34 && !(g >= 6 && g <= 11);
}
bool aprs_loop_test_run(char *m, size_t l) {
    snprintf(m, l, "{}");
    return true;
}
bool aprs_rx_level_sample(char *j, size_t l) {
    snprintf(j, l, "{}");
    return true;
}
bool aprs_tx_test_run(char *m, size_t l) {
    snprintf(m, l, "{}");
    return true;
}
void aprs_service_apply_modem_config(void) {
}
aprs_service_stats_t aprs_service_get_stats(void) {
    aprs_service_stats_t s;
    memset(&s, 0, sizeof s);
    s.radio_rx = 1843;
    s.radio_tx = 212;
    s.rf2inet = 1102;
    s.inet2rf = 14;
    s.digi = 187;
    s.drop = 96;
    s.err = 7;
    s.tx_queue_depth = 0;
    s.tx_queue_limit = 8;
    s.csma_persist_forced = 21;
    s.tx_duty_cycle_pct = 2;
    s.duty_cycle_limit_pct = 0;
    return s;
}
bool aprs_service_send_tnc2(const char *p, size_t l) {
    return true;
}
uint32_t beacon_scheduler_jitter(uint32_t i) {
    return i;
}
void cpu_freq_apply(void) {
}
void gps_apply_config(void) {
}
bool gps_snapshot(gps_data_t *o) {
    memset(o, 0, sizeof *o);
    o->link_up = true;
    o->valid = true;
    o->quality = 1;
    o->mode = 3;
    o->has_position = true;
    o->latitude = -38.7183;
    o->longitude = -62.2663;
    o->has_altitude = true;
    o->altitude_m = 21.4;
    o->has_geoid = true;
    o->geoid_m = 14.2;
    o->has_speed = true;
    o->speed_kmh = 0.4;
    o->has_course = true;
    o->course_deg = 212.5;
    o->has_time = true;
    o->hour = 12;
    o->minute = 17;
    o->second = 42;
    o->has_date = true;
    o->year = 2026;
    o->month = 11;
    o->day = 15;
    o->has_sats_used = true;
    o->sats_used = 9;
    o->has_sats_in_view = true;
    o->sats_in_view = 14;
    o->has_hdop = true;
    o->hdop = 0.9;
    o->has_pdop = true;
    o->pdop = 1.6;
    o->has_vdop = true;
    o->vdop = 1.3;
    o->sentences_ok = 48211;
    o->sentences_bad = 3;
    o->has_fix_age = true;
    o->fix_age_s = 0;
    o->has_link_age = true;
    o->link_age_s = 0;
    return true;
}
void heap_monitor_bracket(const char *a, const char *b) {
}
void igate_get_current_server(char *h, size_t hl, uint16_t *p) {
    snprintf(h, hl, "rotate.aprs2.net");
    *p = 14580;
}
igate_stats_t igate_get_stats(void) {
    igate_stats_t s;
    memset(&s, 0, sizeof s);
    return s;
}
bool igate_is_connected(void) {
    return true;
}
void igate_request_filter_update(void) {
}
void igate_request_reconnect(void) {
}
void igate_uplink_demand(igate_uplink_user_t u, bool w) {
}
bool igate_send_raw(const char *l, size_t n) {
    return true;
}
void message_alarm_configure(bool e, int8_t g) {
}
bool message_alarm_gpio_is_valid(int8_t g) {
    return afsk_gpio_is_output_capable(g);
}
void modem_rx_tuning_sanitize(modem_rx_tuning_t *t) {
}
void sendAPRSMessage(const char *t, const char *x) {
}
bool storage_delete(const char *p) {
    return true;
}
bool storage_format(void) {
    return true;
}
uint32_t storage_generation(void) {
    return 1;
}
bool storage_replace(const char *t, const char *n) {
    return true;
}
bool storage_usage(size_t *u, size_t *t) {
    *u = 36864;
    *t = 524288;
    return true;
}
void storage_write_lock(void) {
}
void storage_write_unlock(void) {
}
void telegram_app_apply_config(void) {
}
bool telegram_app_load(telegram_app_config_t *o) {
    memset(o, 0, sizeof *o);
    o->bulletin_window_s = 3600;
    o->route_station_messages = true;
    return true;
}
void telegram_app_notify_bulletin(const char *a, const char *b, const char *c) {
}
bool telegram_app_save(const telegram_app_config_t *i) {
    return true;
}
void telegram_app_status(telegram_app_status_t *o) {
    memset(o, 0, sizeof *o);
    o->state = TELEGRAM_APP_STATE_DISABLED;
}
void winlink_apply_config(void) {
}
bool winlink_compose_abort(void) {
    return true;
}
bool winlink_compose_begin(const char *a, const char *s) {
    return true;
}
bool winlink_compose_end(void) {
    return true;
}
bool winlink_compose_line(const char *l) {
    return true;
}
bool winlink_forward(unsigned n, const char *a) {
    return true;
}
bool winlink_kill(unsigned n) {
    return true;
}
size_t winlink_last_error(char *o, size_t s) {
    if (s)
        o[0] = 0;
    return 0;
}
bool winlink_list(void) {
    return true;
}
bool winlink_login(void) {
    return true;
}
bool winlink_logoff(void) {
    return true;
}
bool winlink_mail_clear(void) {
    return true;
}
int winlink_mail_count(void) {
    return 0;
}
size_t winlink_mail_next_json(uint32_t a, char *o, size_t s, uint32_t *q) {
    return 0;
}
int winlink_queue_depth(void) {
    return 0;
}
bool winlink_read(unsigned n) {
    return true;
}
bool winlink_reply(unsigned n) {
    return true;
}
bool winlink_send_command(const char *c) {
    return true;
}
uint32_t winlink_session_remaining_sec(void) {
    return 0;
}
winlink_state_t winlink_state(void) {
    return WL_STATE_DISABLED;
}
const char *winlink_state_name(winlink_state_t s) {
    return "disabled";
}
static const char *g_msgs[] = {
    "{\"time\":1763207880,\"dir\":\"rx\",\"call\":\"LU1ABC-9\",\"text\":\"Good morning, I copy your beacon 5/9\",\"status\":\"rx\"}",
    "{\"time\":1763207940,\"dir\":\"tx\",\"call\":\"LU1ABC-9\",\"text\":\"Thanks! Net at 21 tonight?\",\"status\":\"sent\"}",
    "{\"time\":1763208000,\"dir\":\"rx\",\"call\":\"LU1ABC-9\",\"text\":\"Yes, see you there 73\",\"status\":\"rx\"}",
    "{\"time\":1763208060,\"dir\":\"tx\",\"call\":\"LU4JKL-13\",\"text\":\"Hello from the igate\",\"status\":\"pending\"}"
};
size_t message_next_json(uint32_t after, char *out, size_t sz, uint32_t *os) {
    if (after >= 4)
        return 0;
    if (os)
        *os = after + 1;
    return (size_t)snprintf(out, sz, "%s", g_msgs[after]);
}
#include <dirent.h>
struct dirent *__real_readdir(DIR *d);
struct dirent *__wrap_readdir(DIR *d) {
    struct dirent *e;
    while ((e = __real_readdir(d)) && (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))) {
    }
    return e;
}
