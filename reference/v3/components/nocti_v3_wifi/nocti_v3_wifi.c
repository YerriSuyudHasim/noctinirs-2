#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "nocti_v3_wifi.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include "nocti_v3_config.h"

static const char *TAG = "nocti_v3_wifi";

static bool starts_with_command(const uint8_t *data, size_t length, const char *name)
{
    const size_t n = strlen(name);
    if (length < n) return false;
    for (size_t i = 0u; i < n; ++i) {
        char c = (char)data[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c != name[i]) return false;
    }
    return length == n || data[n] == ' ' || data[n] == '\r' || data[n] == '\n';
}

void nocti_v3_wifi_config_defaults(nocti_v3_wifi_config_t *config)
{
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    config->ap_ssid = NOCTI_V3_WIFI_AP_SSID;
    config->ap_password = NOCTI_V3_WIFI_AP_PASSWORD;
    config->ap_channel = NOCTI_V3_WIFI_AP_CHANNEL;
    config->ap_max_connections = NOCTI_V3_WIFI_AP_MAX_CONNECTIONS;
    config->udp_control_port = NOCTI_V3_UDP_CTRL_PORT;
    config->udp_stream_port = NOCTI_V3_UDP_STREAM_PORT;
    config->control_task_stack_bytes = NOCTI_V3_STACK_UDP_CONTROL_BYTES;
    config->stream_task_stack_bytes = NOCTI_V3_STACK_UDP_STREAM_BYTES;
    config->control_task_priority = NOCTI_V3_PRIO_UDP_CONTROL;
    config->stream_task_priority = NOCTI_V3_PRIO_UDP_TX;
    config->control_task_core = NOCTI_V3_CORE_NETWORK;
    config->stream_task_core = NOCTI_V3_CORE_NETWORK;
}

static void update_network_health(nocti_v3_wifi_t *wifi, uint64_t now_us)
{
    bool peer_valid, enabled;
    uint32_t peer;
    uint64_t last;
    portENTER_CRITICAL(&wifi->peer_lock);
    peer_valid = wifi->peer_valid;
    enabled = wifi->stream_enabled;
    peer = wifi->peer_addr_network_order;
    last = wifi->peer_last_activity_us;
    portEXIT_CRITICAL(&wifi->peer_lock);
    /* A configured UDP peer remains active until its Wi-Fi station disconnects
     * or the peer is explicitly replaced. Silence is not an offline signal. */
    (void)now_us;
    (void)last;
    const bool active = peer_valid;
    const bool network_ready = __atomic_load_n(&wifi->network_ready, __ATOMIC_ACQUIRE);
    nocti_v3_health_set_network_state(wifi->health, network_ready,
                                      active, enabled, ntohl(peer));
}

static void set_peer(nocti_v3_wifi_t *wifi, uint32_t address_network_order,
                     uint64_t now_us)
{
    bool changed = false;
    portENTER_CRITICAL(&wifi->peer_lock);
    changed = !wifi->peer_valid ||
              wifi->peer_addr_network_order != address_network_order;
    wifi->peer_addr_network_order = address_network_order;
    wifi->peer_last_activity_us = now_us;
    wifi->peer_valid = true;
    portEXIT_CRITICAL(&wifi->peer_lock);
    update_network_health(wifi, now_us);

    if (changed) {
        const uint32_t host = ntohl(address_network_order);
        ESP_LOGI(TAG, "UDP peer set %u.%u.%u.%u stream_port=%u",
                 (unsigned)((host >> 24) & 0xFFu),
                 (unsigned)((host >> 16) & 0xFFu),
                 (unsigned)((host >> 8) & 0xFFu),
                 (unsigned)(host & 0xFFu),
                 (unsigned)wifi->config.udp_stream_port);
    }
}

static void clear_peer(nocti_v3_wifi_t *wifi)
{
    portENTER_CRITICAL(&wifi->peer_lock);
    wifi->peer_valid = false;
    wifi->stream_enabled = false;
    wifi->peer_addr_network_order = 0u;
    wifi->peer_last_activity_us = 0u;
    wifi->stream_generation++;
    __atomic_store_n(&wifi->stream_reset_requested, true, __ATOMIC_RELEASE);
    portEXIT_CRITICAL(&wifi->peer_lock);
    if (wifi->stream_task != NULL) xTaskNotifyGive(wifi->stream_task);
    update_network_health(wifi, 0u);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    nocti_v3_wifi_t *wifi = (nocti_v3_wifi_t *)arg;
    if (wifi == NULL) return;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        __atomic_store_n(&wifi->ap_started, true, __ATOMIC_RELEASE);
        __atomic_store_n(&wifi->network_ready, true, __ATOMIC_RELEASE);
        const uint32_t stations = __atomic_load_n(&wifi->station_count,
                                                   __ATOMIC_ACQUIRE);
        nocti_v3_health_set_wifi(wifi->health, true, stations);
        update_network_health(wifi, 0u);
        ESP_LOGI(TAG, "SoftAP started");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *event = event_data;
        const uint32_t stations = __atomic_add_fetch(&wifi->station_count, 1u,
                                                      __ATOMIC_ACQ_REL);
        nocti_v3_health_set_wifi(wifi->health,
            __atomic_load_n(&wifi->ap_started, __ATOMIC_ACQUIRE), stations);
        ESP_LOGI(TAG, "station connected aid=%d stations=%lu",
                 event != NULL ? (int)event->aid : -1,
                 (unsigned long)stations);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *event = event_data;
        uint32_t stations = __atomic_load_n(&wifi->station_count, __ATOMIC_ACQUIRE);
        while (stations > 0u &&
               !__atomic_compare_exchange_n(&wifi->station_count, &stations,
                    stations - 1u, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {}
        stations = __atomic_load_n(&wifi->station_count, __ATOMIC_ACQUIRE);
        nocti_v3_health_set_wifi(wifi->health,
            __atomic_load_n(&wifi->ap_started, __ATOMIC_ACQUIRE), stations);
        clear_peer(wifi);
        ESP_LOGI(TAG,
                 "station disconnected aid=%d reason=%d stations=%lu; UDP peer cleared",
                 event != NULL ? (int)event->aid : -1,
                 event != NULL ? (int)event->reason : -1,
                 (unsigned long)stations);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STOP) {
        __atomic_store_n(&wifi->ap_started, false, __ATOMIC_RELEASE);
        __atomic_store_n(&wifi->network_ready, false, __ATOMIC_RELEASE);
        __atomic_store_n(&wifi->station_count, 0u, __ATOMIC_RELEASE);
        nocti_v3_health_set_wifi(wifi->health, false, 0u);
        clear_peer(wifi);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_AP_STAIPASSIGNED) {
        const ip_event_ap_staipassigned_t *event = event_data;
        __atomic_store_n(&wifi->network_ready, true, __ATOMIC_RELEASE);
        const uint32_t stations = __atomic_load_n(&wifi->station_count,
                                                   __ATOMIC_ACQUIRE);
        update_network_health(wifi, 0u);
        if (event != NULL) {
            ESP_LOGI(TAG,
                     "station DHCP assigned IP=" IPSTR " stations=%lu network_ready=1",
                     IP2STR(&event->ip), (unsigned long)stations);
        } else {
            ESP_LOGI(TAG,
                     "station DHCP assigned stations=%lu network_ready=1",
                     (unsigned long)stations);
        }
    }
}

static esp_err_t initialize_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err != ESP_OK) return err;
        err = nvs_flash_init();
    }
    return err;
}

static esp_err_t start_softap(nocti_v3_wifi_t *wifi)
{
    ESP_LOGI(TAG,
             "Wi-Fi init begin mode=SoftAP SSID=%s channel=%u max_conn=%u ctrl=%u stream=%u",
             wifi->config.ap_ssid, (unsigned)wifi->config.ap_channel,
             (unsigned)wifi->config.ap_max_connections,
             (unsigned)wifi->config.udp_control_port,
             (unsigned)wifi->config.udp_stream_port);

    esp_err_t err = initialize_nvs();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init stage=NVS failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Wi-Fi init stage=NVS OK");

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Wi-Fi init stage=esp_netif failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Wi-Fi init stage=esp_netif %s",
             err == ESP_ERR_INVALID_STATE ? "already ready" : "OK");

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Wi-Fi init stage=event_loop failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Wi-Fi init stage=event_loop %s",
             err == ESP_ERR_INVALID_STATE ? "already ready" : "OK");

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif == NULL) {
        ESP_LOGE(TAG, "Wi-Fi init stage=create_ap_netif failed: no memory");
        return ESP_ERR_NO_MEM;
    }
    wifi->ap_netif = ap_netif;
    ESP_LOGI(TAG, "Wi-Fi init stage=create_ap_netif OK");

    const wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init stage=driver failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Wi-Fi init stage=driver OK");

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init stage=storage_ram failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              wifi_event_handler, wifi,
                                              &wifi->wifi_event_instance);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init stage=wifi_event_handler failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    err = esp_event_handler_instance_register(IP_EVENT,
                                              IP_EVENT_AP_STAIPASSIGNED,
                                              wifi_event_handler, wifi,
                                              &wifi->ip_event_instance);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init stage=ip_event_handler failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Wi-Fi init stage=event_handlers OK");

    wifi_config_t ap_config;
    memset(&ap_config, 0, sizeof(ap_config));
    (void)snprintf((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), "%s",
                   wifi->config.ap_ssid);
    ap_config.ap.ssid_len = (uint8_t)strlen((const char *)ap_config.ap.ssid);
    (void)snprintf((char *)ap_config.ap.password,
                   sizeof(ap_config.ap.password), "%s",
                   wifi->config.ap_password != NULL ?
                       wifi->config.ap_password : "");
    ap_config.ap.channel = wifi->config.ap_channel;
    ap_config.ap.max_connection = wifi->config.ap_max_connections;
    ap_config.ap.authmode =
        strlen((const char *)ap_config.ap.password) >= 8u ?
            WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init stage=set_mode failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init stage=set_config failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG,
             "SoftAP config SSID=%s auth=%s channel=%u max_conn=%u",
             wifi->config.ap_ssid,
             ap_config.ap.authmode == WIFI_AUTH_OPEN ? "OPEN" : "WPA2-PSK",
             (unsigned)wifi->config.ap_channel,
             (unsigned)wifi->config.ap_max_connections);

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init stage=start failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Wi-Fi init stage=start submitted; awaiting AP_START event");

    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(ip_info));
    err = esp_netif_get_ip_info(ap_netif, &ip_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init stage=get_ip_info failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    __atomic_store_n(&wifi->network_ready, true, __ATOMIC_RELEASE);
    ESP_LOGI(TAG, "AP IP=" IPSTR " GW=" IPSTR " NM=" IPSTR,
             IP2STR(&ip_info.ip), IP2STR(&ip_info.gw),
             IP2STR(&ip_info.netmask));
    return ESP_OK;
}

static bool peer_snapshot(nocti_v3_wifi_t *wifi, uint64_t now_us,
                          struct sockaddr_in *destination, bool *enabled)
{
    bool valid;
    uint32_t address;
    portENTER_CRITICAL(&wifi->peer_lock);
    valid = wifi->peer_valid;
    *enabled = wifi->stream_enabled;
    address = wifi->peer_addr_network_order;
    portEXIT_CRITICAL(&wifi->peer_lock);
    (void)now_us;
    if (!valid) return false;
    memset(destination, 0, sizeof(*destination));
    destination->sin_family = AF_INET;
    destination->sin_port = htons(wifi->config.udp_stream_port);
    destination->sin_addr.s_addr = address;
    return true;
}

static bool send_payload(nocti_v3_wifi_t *wifi, struct sockaddr_in *destination,
                         nocti_v3_record_type_t type, uint64_t timestamp_us,
                         const void *payload, size_t payload_length)
{
    const size_t bytes = nocti_v3_stream_build_record(
        &wifi->encoder, type, timestamp_us, payload, payload_length,
        wifi->datagram_buffer, sizeof(wifi->datagram_buffer));
    if (bytes == 0u) return false;
    const int sent = sendto(wifi->stream_socket, wifi->datagram_buffer, bytes,
                            MSG_DONTWAIT, (struct sockaddr *)destination,
                            sizeof(*destination));
    const bool ok = sent == (int)bytes;
    uint64_t transmit_us = 0u;
    if (ok) (void)nocti_v3_timebase_now_us(wifi->stream->timebase, &transmit_us);
    nocti_v3_health_record_stream_tx(wifi->health, ok, transmit_us);
    return ok;
}

static void build_phase_payload(const nocti_v3_completed_frame_t *frame,
                                bool led_off,
                                nocti_v3_payload_fnirs_frame_v2_t *payload)
{
    memset(payload, 0, sizeof(*payload));
    payload->version = NOCTI_V3_PAYLOAD_FNIRS_FRAME_V2_VERSION;
    payload->flags = NOCTI_V3_FNIRS_FRAME_F_PAIRED |
                     (led_off ? NOCTI_V3_FNIRS_FRAME_F_LED_OFF : 0u);
    payload->frame_id = (uint16_t)frame->frame_index;
    bool partial = frame->quality != NOCTI_V3_FRAME_QUALITY_VALID;
    for (uint8_t slot = 0u; slot < NOCTI_V3_SLOT_COUNT; ++slot) {
        const nocti_v3_slot_result_t *source = &frame->slot[slot];
        const uint8_t used = led_off ? source->n_i_led_off_used : source->n_on_used;
        payload->n_used[slot] = used;
        if (used > 0u) payload->valid_mask |= 1u << slot;
        if (used < (led_off ? NOCTI_V3_K_TARGET_OFF : NOCTI_V3_K_TARGET_ON)) {
            payload->underk_mask |= 1u << slot;
            partial = true;
        }
        if (source->crc_bad_seen) {
            payload->crc_mask |= 1u << slot;
            partial = true;
        }
        for (uint8_t channel = 0u; channel < 4u; ++channel)
            payload->avg[slot][channel] = led_off ? source->i_led_off[channel] :
                                                   source->i_led_on[channel];
    }
    if (partial) payload->flags |= NOCTI_V3_FNIRS_FRAME_F_PARTIAL;
}

static bool send_session_metadata(nocti_v3_wifi_t *wifi,
                                  struct sockaddr_in *destination,
                                  const nocti_v3_completed_frame_t *frame,
                                  uint32_t *out_records_sent)
{
    if (out_records_sent == NULL) return false;
    *out_records_sent = 0u;
    wifi->encoder.sequence = 0u;
    wifi->encoder.session_id = frame->session_epoch;
    wifi->encoder.t0_us = frame->frame_start_ts;
    wifi->encoded_session = frame->session_epoch;
    wifi->next_time64_due_us = frame->frame_start_ts + NOCTI_V3_STREAM_TIME64_PERIOD_US;
    nocti_v3_payload_session_v1_t session;
    memset(&session, 0, sizeof(session));
    session.version = NOCTI_V3_PAYLOAD_SESSION_V1_VERSION;
    session.session_id = frame->session_epoch;
    session.t0_us = frame->frame_start_ts;
    if (!send_payload(wifi, destination, NOCTI_V3_REC_SESSION,
                      frame->frame_start_ts, &session, sizeof(session))) return false;
    (*out_records_sent)++;
    uint8_t time64[8];
    const uint64_t value = frame->frame_start_ts;
    for (uint8_t i = 0u; i < 8u; ++i)
        time64[i] = (uint8_t)(value >> (56u - 8u * i));
    if (!send_payload(wifi, destination, NOCTI_V3_REC_TIME64,
                      frame->frame_start_ts, time64, sizeof(time64))) return false;
    (*out_records_sent)++;
    uint16_t current_735 = 0u, current_850 = 0u;
    nocti_v3_optical_get_current_profile(wifi->stream->optical,
                                         &current_735, &current_850);
    nocti_v3_payload_fnirs_cfg_v2_t cfg = {
        .version = NOCTI_V3_PAYLOAD_FNIRS_CFG_V2_VERSION,
        .map_mode = NOCTI_V3_FNIRS_MAP_LEDID2_WL,
        .frame_fs_hz = NOCTI_V3_FRAME_RATE_HZ,
        .slot_count = NOCTI_V3_SLOT_COUNT,
        .k_on = NOCTI_V3_K_TARGET_ON,
        .k_off = NOCTI_V3_K_TARGET_OFF,
        .current_735_mA = (uint8_t)current_735,
        .current_850_mA = (uint8_t)current_850,
        .on_settle_us = NOCTI_V3_SETTLE_ON_US,
        .on_window_us = NOCTI_V3_TARGET_WINDOW_ON_US,
        .off_min_settle_us = NOCTI_V3_SETTLE_OFF_US,
        .slot_period_us = NOCTI_V3_SLOT_PERIOD_US,
    };
    if (!send_payload(wifi, destination, NOCTI_V3_REC_FNIRS_CFG,
                      frame->frame_start_ts, &cfg, sizeof(cfg))) return false;
    (*out_records_sent)++;
    return true;
}

static void reset_stream_state(nocti_v3_wifi_t *wifi)
{
    memset(&wifi->encoder, 0, sizeof(wifi->encoder));
    wifi->encoded_session = 0u;
    wifi->next_time64_due_us = 0u;
    __atomic_store_n(&wifi->stream_metadata_pending, true, __ATOMIC_RELEASE);
    const uint32_t flushed = nocti_v3_optical_flush_completed_frames(
        wifi->stream->optical);
    for (uint32_t i = 0u; i < flushed; ++i)
        nocti_v3_optical_record_network_result(wifi->stream->optical,
                                               false, 0u, false);
}

static void stream_task_entry(void *arg)
{
    nocti_v3_wifi_t *wifi = (nocti_v3_wifi_t *)arg;
    nocti_v3_health_set_stream_task(wifi->health, xPortGetCoreID(),
                                    uxTaskGetStackHighWaterMark(NULL));
    if (nocti_v3_optical_bind_frame_consumer(wifi->stream->optical,
                                              xTaskGetCurrentTaskHandle()) != ESP_OK) {
        nocti_v3_optical_record_stream_error(wifi->stream->optical);
        (void)xSemaphoreGive(wifi->stream_task_stopped);
        wifi->stream_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (socket_fd < 0) {
        nocti_v3_optical_record_stream_error(wifi->stream->optical);
        (void)xSemaphoreGive(wifi->stream_task_stopped);
        wifi->stream_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    const int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags >= 0) (void)fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);
    wifi->stream_socket = socket_fd;
    ESP_LOGI(TAG, "UDP stream task ready port=%u core=%d priority=%u",
             (unsigned)wifi->config.udp_stream_port, xPortGetCoreID(),
             (unsigned)wifi->config.stream_task_priority);
    while (!__atomic_load_n(&wifi->stop_requested, __ATOMIC_ACQUIRE)) {
        if (__atomic_exchange_n(&wifi->stream_reset_requested, false,
                                __ATOMIC_ACQ_REL)) {
            reset_stream_state(wifi);
            (void)xSemaphoreGive(wifi->stream_reset_done);
        }
        bool had_frame = false;
        while (nocti_v3_optical_take_completed_frame(wifi->stream->optical,
                                                      &wifi->stream_frame)) {
            had_frame = true;
            uint64_t now_us = 0u;
            (void)nocti_v3_timebase_now_us(wifi->stream->timebase, &now_us);
            struct sockaddr_in destination;
            bool enabled = false;
            const bool peer_active = peer_snapshot(wifi, now_us, &destination, &enabled);
            update_network_health(wifi, now_us);
            const bool network_ready = __atomic_load_n(&wifi->network_ready,
                                                       __ATOMIC_ACQUIRE);
            if (!network_ready || !peer_active || !enabled) {
                nocti_v3_optical_record_network_result(wifi->stream->optical,
                                                       false, 0u, false);
                continue;
            }
            bool metadata_ok = true;
            uint32_t records_sent = 0u;
            if (__atomic_load_n(&wifi->stream_metadata_pending, __ATOMIC_ACQUIRE) ||
                wifi->encoded_session != wifi->stream_frame.session_epoch) {
                metadata_ok = send_session_metadata(wifi, &destination,
                                                    &wifi->stream_frame,
                                                    &records_sent);
                __atomic_store_n(&wifi->stream_metadata_pending, !metadata_ok,
                                 __ATOMIC_RELEASE);
            }
            if (!metadata_ok) {
                nocti_v3_optical_record_network_result(wifi->stream->optical,
                                                       false, records_sent, true);
                continue;
            }
            nocti_v3_payload_fnirs_frame_v2_t on_payload, off_payload;
            build_phase_payload(&wifi->stream_frame, false, &on_payload);
            build_phase_payload(&wifi->stream_frame, true, &off_payload);
            const bool on_ok = send_payload(wifi, &destination,
                                            NOCTI_V3_REC_FNIRS_FRAME,
                                            wifi->stream_frame.frame_end_ts,
                                            &on_payload, sizeof(on_payload));
            const bool off_ok = send_payload(wifi, &destination,
                                             NOCTI_V3_REC_FNIRS_FRAME,
                                             wifi->stream_frame.frame_end_ts,
                                             &off_payload, sizeof(off_payload));
            records_sent += (on_ok ? 1u : 0u) + (off_ok ? 1u : 0u);
            bool frame_ok = on_ok && off_ok;
            if (frame_ok && wifi->stream_frame.frame_end_ts >= wifi->next_time64_due_us) {
                uint8_t time64[8];
                const uint64_t value = wifi->stream_frame.frame_end_ts;
                for (uint8_t i = 0u; i < 8u; ++i)
                    time64[i] = (uint8_t)(value >> (56u - 8u * i));
                const bool time_ok = send_payload(wifi, &destination,
                    NOCTI_V3_REC_TIME64, value, time64, sizeof(time64));
                if (time_ok) { records_sent++; wifi->next_time64_due_us += NOCTI_V3_STREAM_TIME64_PERIOD_US; }
                else frame_ok = false;
            }
            nocti_v3_optical_record_network_result(wifi->stream->optical,
                                                   frame_ok, records_sent,
                                                   !frame_ok);
        }
        nocti_v3_health_set_stream_task(wifi->health, xPortGetCoreID(),
                                        uxTaskGetStackHighWaterMark(NULL));
        if (!had_frame) (void)ulTaskNotifyTake(pdTRUE,
            pdMS_TO_TICKS(NOCTI_V3_UDP_STREAM_WAIT_MS));
    }
    close(socket_fd);
    wifi->stream_socket = -1;
    (void)xSemaphoreGive(wifi->stream_task_stopped);
    wifi->stream_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t request_stream_reset(nocti_v3_wifi_t *wifi)
{
    if (wifi->stream_task == NULL) return ESP_ERR_INVALID_STATE;
    while (xSemaphoreTake(wifi->stream_reset_done, 0u) == pdTRUE) {}
    portENTER_CRITICAL(&wifi->peer_lock);
    wifi->stream_enabled = false;
    wifi->stream_generation++;
    __atomic_store_n(&wifi->stream_reset_requested, true, __ATOMIC_RELEASE);
    portEXIT_CRITICAL(&wifi->peer_lock);
    xTaskNotifyGive(wifi->stream_task);
    if (xSemaphoreTake(wifi->stream_reset_done,
                       pdMS_TO_TICKS(NOCTI_V3_UDP_STREAM_RESET_TIMEOUT_MS)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    return ESP_OK;
}

static void enable_stream_after_start(nocti_v3_wifi_t *wifi)
{
    portENTER_CRITICAL(&wifi->peer_lock);
    wifi->stream_enabled = true;
    portEXIT_CRITICAL(&wifi->peer_lock);
    __atomic_store_n(&wifi->stream_metadata_pending, true, __ATOMIC_RELEASE);
    if (wifi->stream_task != NULL) xTaskNotifyGive(wifi->stream_task);
    ESP_LOGI(TAG, "UDP stream enabled after successful START");
}

static void control_task_entry(void *arg)
{
    nocti_v3_wifi_t *wifi = (nocti_v3_wifi_t *)arg;
    nocti_v3_health_set_udp_task(wifi->health, xPortGetCoreID(),
                                 uxTaskGetStackHighWaterMark(NULL));
    int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (socket_fd < 0) {
        ESP_LOGE(TAG, "UDP control socket creation failed errno=%d", errno);
        (void)xSemaphoreGive(wifi->control_task_stopped);
        wifi->control_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    const struct timeval receive_timeout = {.tv_sec = 0, .tv_usec = 250000};
    (void)setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO,
                     &receive_timeout, sizeof(receive_timeout));
    struct sockaddr_in bind_address;
    memset(&bind_address, 0, sizeof(bind_address));
    bind_address.sin_family = AF_INET;
    bind_address.sin_port = htons(wifi->config.udp_control_port);
    bind_address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(socket_fd, (struct sockaddr *)&bind_address, sizeof(bind_address)) != 0) {
        ESP_LOGE(TAG, "UDP control bind failed port=%u errno=%d",
                 (unsigned)wifi->config.udp_control_port, errno);
        close(socket_fd);
        (void)xSemaphoreGive(wifi->control_task_stopped);
        wifi->control_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    wifi->control_socket = socket_fd;
    ESP_LOGI(TAG, "UDP control listening port=%u stream=%u",
             (unsigned)wifi->config.udp_control_port,
             (unsigned)wifi->config.udp_stream_port);
    while (!__atomic_load_n(&wifi->stop_requested, __ATOMIC_ACQUIRE)) {
        nocti_v3_health_set_udp_task(wifi->health, xPortGetCoreID(),
                                     uxTaskGetStackHighWaterMark(NULL));
        uint8_t receive_buffer[NOCTI_V3_COMMAND_MAX_BYTES];
        struct sockaddr_in source_address;
        socklen_t source_length = sizeof(source_address);
        memset(&source_address, 0, sizeof(source_address));
        const int received = recvfrom(socket_fd, receive_buffer,
                                      sizeof(receive_buffer), 0,
                                      (struct sockaddr *)&source_address,
                                      &source_length);
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            nocti_v3_health_record_udp_tx(wifi->health, false);
            continue;
        }
        if (received == 0) continue;
        uint64_t now_us = 0u;
        (void)nocti_v3_timebase_now_us(wifi->stream->timebase, &now_us);
        nocti_v3_health_record_udp_rx(wifi->health);
        nocti_v3_health_record_command_time(wifi->health, now_us);
        const bool is_start = starts_with_command(receive_buffer, (size_t)received, "START");
        const bool is_stop = starts_with_command(receive_buffer, (size_t)received, "STOP");
        /* Any received command proves peer activity. The peer remains configured
         * until its Wi-Fi station disconnects; command silence is not offline. */
        set_peer(wifi, source_address.sin_addr.s_addr, now_us);
        esp_err_t pre_reset_error = ESP_OK;
        if (is_start || is_stop) pre_reset_error = request_stream_reset(wifi);
        size_t reply_length = 0u;
        esp_err_t command_error = ESP_OK;
        if (is_start && pre_reset_error != ESP_OK) {
            reply_length = (size_t)snprintf(
                wifi->reply_buffer, sizeof(wifi->reply_buffer),
                "START_ERR reason=stream_reset_timeout err=%s acquisition=not_started\n",
                esp_err_to_name(pre_reset_error));
            if (reply_length >= sizeof(wifi->reply_buffer))
                reply_length = sizeof(wifi->reply_buffer) - 1u;
            command_error = pre_reset_error;
            nocti_v3_health_record_command(wifi->health, true, false);
            nocti_v3_optical_record_stream_error(wifi->stream->optical);
        } else {
            if (is_stop && pre_reset_error != ESP_OK)
                nocti_v3_optical_record_stream_error(wifi->stream->optical);
            command_error = nocti_v3_stream_handle_command(
                wifi->stream, receive_buffer, (size_t)received, wifi->reply_buffer,
                sizeof(wifi->reply_buffer), &reply_length);
            if (is_start && command_error == ESP_OK) enable_stream_after_start(wifi);
        }
        if (reply_length != 0u) {
            const int sent = sendto(socket_fd, wifi->reply_buffer, reply_length, 0,
                                    (struct sockaddr *)&source_address, source_length);
            nocti_v3_health_record_udp_tx(wifi->health, sent == (int)reply_length);
        }
        update_network_health(wifi, now_us);
    }
    close(socket_fd);
    wifi->control_socket = -1;
    (void)xSemaphoreGive(wifi->control_task_stopped);
    wifi->control_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t nocti_v3_wifi_init(nocti_v3_wifi_t *wifi,
                             const nocti_v3_wifi_config_t *config,
                             nocti_v3_stream_t *stream,
                             nocti_v3_health_t *health)
{
    if (wifi == NULL || config == NULL || stream == NULL || health == NULL ||
        config->ap_ssid == NULL || config->udp_control_port == 0u ||
        config->udp_stream_port == 0u) return ESP_ERR_INVALID_ARG;
    memset(wifi, 0, sizeof(*wifi));
    wifi->control_socket = -1;
    wifi->stream_socket = -1;
    wifi->peer_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    wifi->config = *config;
    wifi->stream = stream;
    wifi->health = health;
    __atomic_store_n(&wifi->stream_metadata_pending, true, __ATOMIC_RELEASE);
    wifi->stream_reset_done = xSemaphoreCreateBinaryStatic(&wifi->stream_reset_done_storage);
    wifi->control_task_stopped = xSemaphoreCreateBinaryStatic(
        &wifi->control_task_stopped_storage);
    wifi->stream_task_stopped = xSemaphoreCreateBinaryStatic(
        &wifi->stream_task_stopped_storage);
    if (wifi->stream_reset_done == NULL || wifi->control_task_stopped == NULL ||
        wifi->stream_task_stopped == NULL) return ESP_ERR_NO_MEM;
    esp_err_t err = start_softap(wifi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi SoftAP initialization failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    /* Mark initialized before task creation so any partial-init failure can use
     * the same bounded, acknowledgement-based cleanup path as normal stop. */
    wifi->initialized = true;
    if (xTaskCreatePinnedToCore(stream_task_entry, "nocti_v3_udp_stream",
          config->stream_task_stack_bytes, wifi, config->stream_task_priority,
          &wifi->stream_task, config->stream_task_core) != pdPASS) {
        ESP_LOGE(TAG, "UDP stream task creation failed stack=%lu core=%d priority=%u",
                 (unsigned long)config->stream_task_stack_bytes,
                 config->stream_task_core,
                 (unsigned)config->stream_task_priority);
        (void)nocti_v3_wifi_stop(wifi);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "UDP stream task created stack=%lu core=%d priority=%u",
             (unsigned long)config->stream_task_stack_bytes,
             config->stream_task_core,
             (unsigned)config->stream_task_priority);
    if (xTaskCreatePinnedToCore(control_task_entry, "nocti_v3_udp_ctrl",
          config->control_task_stack_bytes, wifi, config->control_task_priority,
          &wifi->control_task, config->control_task_core) != pdPASS) {
        ESP_LOGE(TAG, "UDP control task creation failed stack=%lu core=%d priority=%u",
                 (unsigned long)config->control_task_stack_bytes,
                 config->control_task_core,
                 (unsigned)config->control_task_priority);
        (void)nocti_v3_wifi_stop(wifi);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "UDP control task created stack=%lu core=%d priority=%u",
             (unsigned long)config->control_task_stack_bytes,
             config->control_task_core,
             (unsigned)config->control_task_priority);
    update_network_health(wifi, 0u);
    ESP_LOGI(TAG, "Wi-Fi/UDP ready ctrl=%u stream=%u",
             (unsigned)config->udp_control_port,
             (unsigned)config->udp_stream_port);
    return ESP_OK;
}

esp_err_t nocti_v3_wifi_stop(nocti_v3_wifi_t *wifi)
{
    if (wifi == NULL) return ESP_ERR_INVALID_ARG;
    if (!wifi->initialized) return ESP_OK;

    __atomic_store_n(&wifi->stop_requested, true, __ATOMIC_RELEASE);
    TaskHandle_t control_task = wifi->control_task;
    TaskHandle_t stream_task = wifi->stream_task;
    if (control_task != NULL) xTaskNotifyGive(control_task);
    if (stream_task != NULL) xTaskNotifyGive(stream_task);

    esp_err_t first_error = ESP_OK;
    const TickType_t timeout = pdMS_TO_TICKS(NOCTI_V3_NETWORK_TASK_STOP_TIMEOUT_MS);
    if (control_task != NULL &&
        xSemaphoreTake(wifi->control_task_stopped, timeout) != pdTRUE) {
        first_error = ESP_ERR_TIMEOUT;
    }
    if (stream_task != NULL &&
        xSemaphoreTake(wifi->stream_task_stopped, timeout) != pdTRUE &&
        first_error == ESP_OK) {
        first_error = ESP_ERR_TIMEOUT;
    }

    esp_err_t err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_INIT) err = ESP_OK;
    if (err != ESP_OK && first_error == ESP_OK) first_error = err;
    if (wifi->wifi_event_instance != NULL) {
        err = esp_event_handler_instance_unregister(
            WIFI_EVENT, ESP_EVENT_ANY_ID, wifi->wifi_event_instance);
        if (err != ESP_OK && first_error == ESP_OK) first_error = err;
        wifi->wifi_event_instance = NULL;
    }
    if (wifi->ip_event_instance != NULL) {
        err = esp_event_handler_instance_unregister(
            IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, wifi->ip_event_instance);
        if (err != ESP_OK && first_error == ESP_OK) first_error = err;
        wifi->ip_event_instance = NULL;
    }
    wifi->initialized = false;
    __atomic_store_n(&wifi->network_ready, false, __ATOMIC_RELEASE);
    __atomic_store_n(&wifi->ap_started, false, __ATOMIC_RELEASE);
    clear_peer(wifi);
    return first_error;
}
