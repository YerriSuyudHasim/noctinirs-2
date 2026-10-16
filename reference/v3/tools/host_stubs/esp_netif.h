#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef void esp_netif_t; typedef struct {uint32_t addr;} esp_ip4_addr_t; typedef struct {esp_ip4_addr_t ip,netmask,gw;} esp_netif_ip_info_t; typedef struct {esp_ip4_addr_t ip;} ip_event_ap_staipassigned_t;
static inline esp_err_t esp_netif_init(void){return ESP_OK;}
static inline esp_netif_t *esp_netif_create_default_wifi_ap(void){return (void*)1;}
static inline esp_err_t esp_netif_get_ip_info(esp_netif_t*n,esp_netif_ip_info_t*i){(void)n;if(i)i->ip.addr=0;return ESP_OK;}
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) 0u,0u,0u,0u
