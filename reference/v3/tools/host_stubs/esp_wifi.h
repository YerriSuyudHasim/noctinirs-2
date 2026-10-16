#pragma once
#include <stdint.h>
#include "esp_err.h"
#define WIFI_EVENT_AP_START 1
#define WIFI_EVENT_AP_STACONNECTED 2
#define WIFI_EVENT_AP_STADISCONNECTED 3
#define WIFI_EVENT_AP_STOP 4
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_WPA2_PSK 1
#define WIFI_MODE_AP 1
#define WIFI_IF_AP 1
#define WIFI_STORAGE_RAM 1
typedef struct {int aid;} wifi_event_ap_staconnected_t; typedef struct {int aid; int reason;} wifi_event_ap_stadisconnected_t;
typedef struct {int dummy;} wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
typedef struct {struct {uint8_t ssid[32];uint8_t password[64];uint8_t ssid_len;uint8_t channel;uint8_t max_connection;int authmode;}ap;} wifi_config_t;
static inline esp_err_t esp_wifi_init(const wifi_init_config_t*c){(void)c;return ESP_OK;}
static inline esp_err_t esp_wifi_set_storage(int s){(void)s;return ESP_OK;}
static inline esp_err_t esp_wifi_set_mode(int m){(void)m;return ESP_OK;}
static inline esp_err_t esp_wifi_set_config(int i,const wifi_config_t*c){(void)i;(void)c;return ESP_OK;}
static inline esp_err_t esp_wifi_start(void){return ESP_OK;}
static inline esp_err_t esp_wifi_stop(void){return ESP_OK;}
