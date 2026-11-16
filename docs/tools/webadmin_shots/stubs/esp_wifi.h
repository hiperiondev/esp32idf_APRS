#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
typedef enum { WIFI_MODE_NULL=0, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA } wifi_mode_t;
typedef enum { WIFI_IF_STA=0, WIFI_IF_AP } wifi_interface_t;
typedef enum { WIFI_AUTH_OPEN=0, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK, WIFI_AUTH_WPA_WPA2_PSK, WIFI_AUTH_WPA3_PSK=6 } wifi_auth_mode_t;
typedef struct { uint8_t bssid[6]; uint8_t ssid[33]; uint8_t primary; int8_t rssi; wifi_auth_mode_t authmode; } wifi_ap_record_t;
typedef struct { uint8_t ssid[32]; uint8_t password[64]; uint8_t ssid_len; uint8_t channel; wifi_auth_mode_t authmode; uint8_t ssid_hidden; uint8_t max_connection; } wifi_ap_config_t;
typedef struct { uint8_t ssid[32]; uint8_t password[64]; } wifi_sta_config_t;
typedef union { wifi_ap_config_t ap; wifi_sta_config_t sta; } wifi_config_t;
typedef struct { uint8_t *ssid; uint8_t *bssid; uint8_t channel; bool show_hidden; } wifi_scan_config_t;
esp_err_t esp_wifi_set_mode(wifi_mode_t m);
esp_err_t esp_wifi_get_mode(wifi_mode_t *m);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *r);
esp_err_t esp_wifi_set_config(wifi_interface_t i, wifi_config_t *c);
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *c, bool block);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *n, wifi_ap_record_t *r);
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *n);
esp_err_t esp_wifi_clear_ap_list(void);
#define ESP_ERR_WIFI_STATE 0x3007
esp_err_t esp_wifi_connect(void);
