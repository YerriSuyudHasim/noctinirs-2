#include "nn2_wifi.h"
#include "sdkconfig.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <stdio.h>
#include <string.h>

#if !CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
#error "NoctiNIRS SoftAP requires ESP_WIFI_SOFTAP_SAE_SUPPORT"
#endif

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static esp_netif_t *s_ap;
static esp_event_handler_instance_t s_handler;
static bool s_initialized, s_started, s_accept_events, s_active;
static uint8_t s_clients;
static uint32_t s_epoch;
static esp_err_t s_error;
static esp_netif_ip_info_t s_ip;

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base != WIFI_EVENT) return;
    taskENTER_CRITICAL(&s_lock);
    if (s_accept_events) {
        if (id == WIFI_EVENT_AP_START) {
            s_active = true;
            s_clients = 0;
            ++s_epoch;
        } else if (id == WIFI_EVENT_AP_STOP) {
            s_active = false;
            s_clients = 0;
            ++s_epoch;
        } else if (id == WIFI_EVENT_AP_STACONNECTED) {
            s_clients = 1; /* max_connection=1: one laptop owns the radio link. */
        } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
            s_clients = 0;
            ++s_epoch; /* Revoke old UDP ownership even after a quick reconnect. */
        }
    }
    taskEXIT_CRITICAL(&s_lock);
}

esp_err_t nn2_wifi_init(void)
{
    if (s_initialized) return ESP_ERR_INVALID_STATE;
    esp_err_t result = esp_netif_init();
    if (result != ESP_OK) return result;
    result = esp_event_loop_create_default();
    if (result != ESP_OK) return result;
    s_ap = esp_netif_create_default_wifi_ap();
    if (!s_ap) { esp_event_loop_delete_default(); return ESP_ERR_NO_MEM; }
    result = esp_netif_dhcps_stop(s_ap);
    if (result != ESP_OK && result != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) goto fail_netif;
    IP4_ADDR(&s_ip.ip, 192,168,4,1);
    IP4_ADDR(&s_ip.gw, 192,168,4,1);
    IP4_ADDR(&s_ip.netmask, 255,255,255,0);
    result = esp_netif_set_ip_info(s_ap, &s_ip);
    if (result != ESP_OK) goto fail_netif;
    result = esp_netif_dhcps_start(s_ap);
    if (result != ESP_OK) goto fail_netif;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    init.nvs_enable = false; /* Old STA credentials cannot override the AP. */
    result = esp_wifi_init(&init);
    if (result != ESP_OK) goto fail_netif;
    result = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
        wifi_event, NULL, &s_handler);
    if (result != ESP_OK) { esp_wifi_deinit(); goto fail_netif; }
    s_initialized = true;
    return ESP_OK;
fail_netif:
    esp_netif_destroy_default_wifi(s_ap);
    s_ap = NULL;
    esp_event_loop_delete_default();
    return result;
}

esp_err_t nn2_wifi_start(void)
{
    if (!s_initialized || s_started) return ESP_ERR_INVALID_STATE;
    /* Owner-selected PUBLIC prototype credentials, not a production secret. */
    wifi_config_t config = { .ap = {
        .ssid = "NoctiNIRS", .ssid_len = 9,
        .password = "123456789", .channel = 1, .max_connection = 1,
        .authmode = WIFI_AUTH_WPA2_WPA3_PSK,
        .pairwise_cipher = WIFI_CIPHER_TYPE_CCMP,
        .pmf_cfg = { .required = false },
        .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        .beacon_interval = 100, .dtim_period = 1,
    }};
    esp_err_t result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result == ESP_OK) result = esp_wifi_set_mode(WIFI_MODE_AP);
    if (result == ESP_OK) result = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (result == ESP_OK) {
        taskENTER_CRITICAL(&s_lock);
        s_accept_events = true;
        taskEXIT_CRITICAL(&s_lock);
        result = esp_wifi_start();
    }
    if (result != ESP_OK) {
        taskENTER_CRITICAL(&s_lock);
        s_accept_events = false;
        s_active = false;
        s_clients = 0;
        ++s_epoch;
        taskEXIT_CRITICAL(&s_lock);
    }
    s_started = result == ESP_OK;
    s_error = result;
    return result;
}

esp_err_t nn2_wifi_bind_socket(int fd)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    struct ifreq interface = {0};
    esp_err_t result = esp_netif_get_netif_impl_name(s_ap, interface.ifr_name);
    if (result != ESP_OK) return result;
    return setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, &interface, sizeof(interface)) == 0
        ? ESP_OK : ESP_FAIL;
}

nn2_wifi_status_t nn2_wifi_get_status(void)
{
    nn2_wifi_status_t status = {.started=s_started, .last_error=s_error};
    taskENTER_CRITICAL(&s_lock);
    status.ap_active = s_active;
    status.clients = s_clients;
    status.connected = s_active && s_clients != 0;
    status.link_epoch = s_epoch;
    taskEXIT_CRITICAL(&s_lock);
    snprintf(status.ipv4,sizeof(status.ipv4),IPSTR,IP2STR(&s_ip.ip));
    return status;
}

esp_err_t nn2_wifi_stop(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    taskENTER_CRITICAL(&s_lock);
    s_accept_events = false;
    s_active = false;
    s_clients = 0;
    ++s_epoch;
    taskEXIT_CRITICAL(&s_lock);
    esp_err_t result = s_started ? esp_wifi_stop() : ESP_OK;
    if (result == ESP_OK) s_started = false;
    s_error = result;
    return result;
}
