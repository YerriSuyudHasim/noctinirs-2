#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nocti_v3_health.h"
#include "nocti_v3_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *ap_ssid;
    const char *ap_password;
    uint8_t ap_channel;
    uint8_t ap_max_connections;
    uint16_t udp_control_port;
    uint16_t udp_stream_port;
    uint32_t control_task_stack_bytes;
    uint32_t stream_task_stack_bytes;
    UBaseType_t control_task_priority;
    UBaseType_t stream_task_priority;
    BaseType_t control_task_core;
    BaseType_t stream_task_core;
} nocti_v3_wifi_config_t;

typedef struct {
    bool initialized;
    volatile bool ap_started;
    volatile bool network_ready;
    volatile bool stop_requested;
    volatile uint32_t station_count;
    int control_socket;
    int stream_socket;
    TaskHandle_t control_task;
    TaskHandle_t stream_task;
    void *ap_netif;
    esp_event_handler_instance_t wifi_event_instance;
    esp_event_handler_instance_t ip_event_instance;
    nocti_v3_wifi_config_t config;
    nocti_v3_stream_t *stream;
    nocti_v3_health_t *health;

    portMUX_TYPE peer_lock;
    volatile bool peer_valid;
    volatile bool stream_enabled;
    volatile bool stream_reset_requested;
    volatile bool stream_metadata_pending;
    uint32_t peer_addr_network_order;
    uint64_t peer_last_activity_us;
    uint32_t stream_generation;
    SemaphoreHandle_t stream_reset_done;
    StaticSemaphore_t stream_reset_done_storage;
    SemaphoreHandle_t control_task_stopped;
    StaticSemaphore_t control_task_stopped_storage;
    SemaphoreHandle_t stream_task_stopped;
    StaticSemaphore_t stream_task_stopped_storage;

    nocti_v3_stream_encoder_t encoder;
    uint32_t encoded_session;
    uint64_t next_time64_due_us;
    nocti_v3_completed_frame_t stream_frame;
    uint8_t datagram_buffer[NOCTI_V3_STREAM_MAX_FRAME_BYTES];
    char reply_buffer[NOCTI_V3_REPLY_MAX_BYTES];
} nocti_v3_wifi_t;

void nocti_v3_wifi_config_defaults(nocti_v3_wifi_config_t *config);
esp_err_t nocti_v3_wifi_init(nocti_v3_wifi_t *wifi,
                             const nocti_v3_wifi_config_t *config,
                             nocti_v3_stream_t *stream,
                             nocti_v3_health_t *health);
esp_err_t nocti_v3_wifi_stop(nocti_v3_wifi_t *wifi);

#ifdef __cplusplus
}
#endif
