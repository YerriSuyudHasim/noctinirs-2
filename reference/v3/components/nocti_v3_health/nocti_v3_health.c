#include "nocti_v3_health.h"

#include <string.h>

void nocti_v3_health_init(nocti_v3_health_t *health)
{
    if (health == NULL) return;
    memset(health, 0, sizeof(*health));
    health->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    health->data.boot_count = 1u;
    health->data.lifecycle = NOCTI_V3_STATE_STOPPED;
    health->data.mode = NOCTI_V3_MODE_STOPPED;
    health->data.last_fault = NOCTI_V3_FAULT_NONE;
    health->data.udp_task_core = -1;
    health->data.stream_task_core = -1;
    health->data.health_task_core = -1;
}

void nocti_v3_health_snapshot(nocti_v3_health_t *health,
                              nocti_v3_health_snapshot_t *out_snapshot)
{
    if (health == NULL || out_snapshot == NULL) return;
    portENTER_CRITICAL(&health->lock);
    *out_snapshot = health->data;
    portEXIT_CRITICAL(&health->lock);
}

void nocti_v3_health_set_startup(nocti_v3_health_t *health,
                                 bool timing_valid,
                                 bool timebase_ready,
                                 bool board_gpio_safe,
                                 bool board_i2c_ready,
                                 bool led_fail_safe)
{
    if (health == NULL) return;
    portENTER_CRITICAL(&health->lock);
    health->data.timing_valid = timing_valid;
    health->data.timebase_ready = timebase_ready;
    health->data.board_gpio_safe = board_gpio_safe;
    health->data.board_i2c_ready = board_i2c_ready;
    health->data.led_fail_safe = led_fail_safe;
    portEXIT_CRITICAL(&health->lock);
}

void nocti_v3_health_set_wifi(nocti_v3_health_t *health,
                              bool ap_started,
                              uint32_t station_count)
{
    if (health == NULL) return;
    portENTER_CRITICAL(&health->lock);
    health->data.wifi_ap_started = ap_started;
    health->data.wifi_station_count = station_count;
    portEXIT_CRITICAL(&health->lock);
}

void nocti_v3_health_set_udp_task(nocti_v3_health_t *health,
                                  int task_core,
                                  uint32_t stack_watermark_bytes)
{
    if (health == NULL) return;
    portENTER_CRITICAL(&health->lock);
    health->data.udp_task_core = task_core;
    health->data.udp_task_stack_watermark_bytes = stack_watermark_bytes;
    portEXIT_CRITICAL(&health->lock);
}


void nocti_v3_health_set_stream_task(nocti_v3_health_t *health,
                                     int task_core,
                                     uint32_t stack_watermark_bytes)
{
    if (health == NULL) return;
    portENTER_CRITICAL(&health->lock);
    health->data.stream_task_core = task_core;
    health->data.stream_task_stack_watermark_bytes = stack_watermark_bytes;
    portEXIT_CRITICAL(&health->lock);
}

void nocti_v3_health_set_logger_task(nocti_v3_health_t *health,
                                     int task_core,
                                     uint32_t stack_watermark_bytes)
{
    if (health == NULL) return;
    portENTER_CRITICAL(&health->lock);
    health->data.health_task_core = task_core;
    health->data.health_task_stack_watermark_bytes = stack_watermark_bytes;
    portEXIT_CRITICAL(&health->lock);
}

void nocti_v3_health_set_network_state(nocti_v3_health_t *health,
                                       bool network_ready,
                                       bool udp_peer_active,
                                       bool stream_enabled,
                                       uint32_t peer_ip4)
{
    if (health == NULL) return;
    portENTER_CRITICAL(&health->lock);
    health->data.network_ready = network_ready;
    health->data.udp_peer_active = udp_peer_active;
    health->data.stream_enabled = stream_enabled;
    health->data.udp_peer_ip4 = peer_ip4;
    portEXIT_CRITICAL(&health->lock);
}

void nocti_v3_health_record_command_time(nocti_v3_health_t *health,
                                         uint64_t timestamp_us)
{
    if (health == NULL) return;
    portENTER_CRITICAL(&health->lock);
    health->data.last_command_receive_us = timestamp_us;
    portEXIT_CRITICAL(&health->lock);
}

void nocti_v3_health_record_stream_tx(nocti_v3_health_t *health,
                                      bool success,
                                      uint64_t timestamp_us)
{
    if (health == NULL) return;
    portENTER_CRITICAL(&health->lock);
    if (success) {
        health->data.udp_tx_packets++;
        health->data.last_successful_transmit_us = timestamp_us;
    } else {
        health->data.udp_socket_errors++;
    }
    portEXIT_CRITICAL(&health->lock);
}

void nocti_v3_health_set_sensor_fault(nocti_v3_health_t *health, bool faulted)
{
    if (health == NULL) return;
    portENTER_CRITICAL(&health->lock);
    health->data.sensor_fault = faulted;
    portEXIT_CRITICAL(&health->lock);
}

void nocti_v3_health_set_lifecycle(nocti_v3_health_t *health,
                                   nocti_v3_lifecycle_state_t lifecycle,
                                   nocti_v3_mode_t mode)
{
    if (health == NULL) return;
    portENTER_CRITICAL(&health->lock);
    health->data.lifecycle = lifecycle;
    health->data.mode = mode;
    portEXIT_CRITICAL(&health->lock);
}

void nocti_v3_health_record_fault(nocti_v3_health_t *health,
                                  nocti_v3_fault_code_t fault,
                                  int32_t esp_error)
{
    if (health == NULL) return;
    portENTER_CRITICAL(&health->lock);
    health->data.startup_errors++;
    health->data.last_fault = fault;
    health->data.last_esp_error = esp_error;
    portEXIT_CRITICAL(&health->lock);
}

void nocti_v3_health_record_command(nocti_v3_health_t *health,
                                    bool supported,
                                    bool success)
{
    if (health == NULL) return;
    portENTER_CRITICAL(&health->lock);
    health->data.command_count++;
    if (!supported) health->data.unsupported_commands++;
    if (!success) health->data.command_errors++;
    portEXIT_CRITICAL(&health->lock);
}

void nocti_v3_health_record_udp_rx(nocti_v3_health_t *health)
{
    if (health == NULL) return;
    portENTER_CRITICAL(&health->lock);
    health->data.udp_rx_packets++;
    portEXIT_CRITICAL(&health->lock);
}

void nocti_v3_health_record_udp_tx(nocti_v3_health_t *health,
                                   bool success)
{
    if (health == NULL) return;
    portENTER_CRITICAL(&health->lock);
    if (success) health->data.udp_tx_packets++;
    else health->data.udp_socket_errors++;
    portEXIT_CRITICAL(&health->lock);
}

void nocti_v3_health_reset_runtime_counters(nocti_v3_health_t *health)
{
    if (health == NULL) return;
    portENTER_CRITICAL(&health->lock);
    health->data.command_count = 0u;
    health->data.command_errors = 0u;
    health->data.unsupported_commands = 0u;
    health->data.udp_rx_packets = 0u;
    health->data.udp_tx_packets = 0u;
    health->data.udp_socket_errors = 0u;
    /* Preserve task/core state, online state, lifecycle, timestamps, and faults. */
    portEXIT_CRITICAL(&health->lock);
}
