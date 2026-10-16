#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef const void *esp_event_base_t; typedef void *esp_event_handler_instance_t;
static const int _wifi_event_base_obj=0; static const int _ip_event_base_obj=0;
#define WIFI_EVENT ((esp_event_base_t)&_wifi_event_base_obj)
#define IP_EVENT ((esp_event_base_t)&_ip_event_base_obj)
#define ESP_EVENT_ANY_ID (-1)
#define IP_EVENT_AP_STAIPASSIGNED 1
static inline esp_err_t esp_event_loop_create_default(void){return ESP_OK;}
static inline esp_err_t esp_event_handler_instance_register(esp_event_base_t b,int32_t i,void(*h)(void*,esp_event_base_t,int32_t,void*),void*a,esp_event_handler_instance_t*o){(void)b;(void)i;(void)h;(void)a;if(o)*o=(void*)1;return ESP_OK;}
static inline esp_err_t esp_event_handler_instance_unregister(esp_event_base_t b,int32_t i,esp_event_handler_instance_t h){(void)b;(void)i;(void)h;return ESP_OK;}
