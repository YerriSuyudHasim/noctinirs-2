#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "nocti_v3_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t boot_count;
    uint32_t command_count;
    uint32_t command_errors;
    uint32_t unsupported_commands;
    uint32_t udp_rx_packets;
    uint32_t udp_tx_packets;
    uint32_t udp_socket_errors;
    uint32_t wifi_station_count;
    uint32_t startup_errors;
    uint32_t udp_task_stack_watermark_bytes;
    uint32_t stream_task_stack_watermark_bytes;
    uint32_t health_task_stack_watermark_bytes;
    int udp_task_core;
    int stream_task_core;
    int health_task_core;
    uint64_t last_command_receive_us;
    uint64_t last_successful_transmit_us;
    uint32_t udp_peer_ip4;
    bool network_ready;
    bool udp_peer_active;
    bool stream_enabled;
    bool sensor_fault;
    bool timing_valid;
    bool timebase_ready;
    bool board_gpio_safe;
    bool board_i2c_ready;
    bool led_fail_safe;
    bool wifi_ap_started;
    nocti_v3_lifecycle_state_t lifecycle;
    nocti_v3_mode_t mode;
    nocti_v3_fault_code_t last_fault;
    int32_t last_esp_error;
} nocti_v3_health_snapshot_t;

typedef struct {
    portMUX_TYPE lock;
    nocti_v3_health_snapshot_t data;
} nocti_v3_health_t;

void nocti_v3_health_init(nocti_v3_health_t *health);
void nocti_v3_health_snapshot(nocti_v3_health_t *health,
                              nocti_v3_health_snapshot_t *out_snapshot);
void nocti_v3_health_set_startup(nocti_v3_health_t *health,
                                 bool timing_valid,
                                 bool timebase_ready,
                                 bool board_gpio_safe,
                                 bool board_i2c_ready,
                                 bool led_fail_safe);
void nocti_v3_health_set_wifi(nocti_v3_health_t *health,
                              bool ap_started,
                              uint32_t station_count);
void nocti_v3_health_set_udp_task(nocti_v3_health_t *health,
                                  int task_core,
                                  uint32_t stack_watermark_bytes);
void nocti_v3_health_set_stream_task(nocti_v3_health_t *health,
                                     int task_core,
                                     uint32_t stack_watermark_bytes);
void nocti_v3_health_set_logger_task(nocti_v3_health_t *health,
                                     int task_core,
                                     uint32_t stack_watermark_bytes);
void nocti_v3_health_set_network_state(nocti_v3_health_t *health,
                                       bool network_ready,
                                       bool udp_peer_active,
                                       bool stream_enabled,
                                       uint32_t peer_ip4);
void nocti_v3_health_record_command_time(nocti_v3_health_t *health,
                                         uint64_t timestamp_us);
void nocti_v3_health_record_stream_tx(nocti_v3_health_t *health,
                                      bool success,
                                      uint64_t timestamp_us);
void nocti_v3_health_set_sensor_fault(nocti_v3_health_t *health, bool faulted);
void nocti_v3_health_set_lifecycle(nocti_v3_health_t *health,
                                   nocti_v3_lifecycle_state_t lifecycle,
                                   nocti_v3_mode_t mode);
void nocti_v3_health_record_fault(nocti_v3_health_t *health,
                                  nocti_v3_fault_code_t fault,
                                  int32_t esp_error);
void nocti_v3_health_record_command(nocti_v3_health_t *health,
                                    bool supported,
                                    bool success);
void nocti_v3_health_record_udp_rx(nocti_v3_health_t *health);
void nocti_v3_health_record_udp_tx(nocti_v3_health_t *health,
                                   bool success);
void nocti_v3_health_reset_runtime_counters(nocti_v3_health_t *health);

#ifdef __cplusplus
}
#endif
