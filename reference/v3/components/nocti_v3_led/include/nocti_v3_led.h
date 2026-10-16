#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nocti_v3_board.h"
#include "nocti_v3_health.h"
#include "nocti_v3_timebase.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NOCTI_V3_LED_UNINITIALIZED = 0,
    NOCTI_V3_LED_RESET_HELD_SAFE,
    NOCTI_V3_LED_CONFIGURED_ALL_OFF,
    NOCTI_V3_LED_RUNTIME_READY,
    NOCTI_V3_LED_RECOVERING,
    NOCTI_V3_LED_FAULT,
} nocti_v3_led_state_t;

typedef enum {
    NOCTI_V3_LED_COMMAND_SLOT_ON = 1,
    NOCTI_V3_LED_COMMAND_ALL_OFF,
    NOCTI_V3_LED_COMMAND_FORCE_OFF,
} nocti_v3_led_command_type_t;

typedef enum {
    NOCTI_V3_LED_RESULT_DONE = 0,
    NOCTI_V3_LED_RESULT_NACK,
    NOCTI_V3_LED_RESULT_TIMEOUT,
    NOCTI_V3_LED_RESULT_SUBMIT_ERROR,
} nocti_v3_led_result_t;

typedef struct {
    nocti_v3_led_command_type_t type;
    uint8_t slot_index;
    uint32_t command_id;
    uint32_t session_epoch;
    uint32_t frame_index;
    uint32_t slot_epoch;
    uint64_t request_timestamp_us;
} nocti_v3_led_request_t;

typedef struct {
    nocti_v3_led_request_t request;
    nocti_v3_led_result_t result;
    esp_err_t submit_result;
    uint64_t i2c_submit_timestamp_us;
    uint64_t i2c_complete_timestamp_us;
    int callback_core;
} nocti_v3_led_completion_t;

typedef struct {
    uint64_t led_errors;
    uint64_t i2c_nack;
    uint64_t i2c_timeout;
    uint64_t i2c_queue_full;
    uint64_t i2c_completion_ring_overflow;
    uint64_t stale_i2c_completion;
    uint64_t recovery_count;
    uint64_t hardware_reset_count;
    uint32_t led_commit_latency_max_us;
    uint32_t consecutive_errors;
    uint32_t safe_off_count;
    uint16_t current_735_ma;
    uint16_t current_850_ma;
    uint32_t led_task_stack_watermark_bytes;
    uint32_t recovery_task_stack_watermark_bytes;
    int led_task_core;
    int recovery_task_core;
    int i2c_init_core;
    int i2c_callback_core;
    bool initialized;
    bool runtime_ready;
    bool transaction_pending;
    bool recovery_pending;
    nocti_v3_led_state_t state;
    esp_err_t last_error;
} nocti_v3_led_health_snapshot_t;

typedef struct {
    nocti_v3_board_t *board;
    nocti_v3_timebase_t *timebase;
    nocti_v3_health_t *health;
    nocti_v3_led_state_t state;
    uint32_t safe_off_count;
    uint16_t current_735_ma;
    uint16_t current_850_ma;
    int32_t last_error;
    void *runtime;
} nocti_v3_led_t;

esp_err_t nocti_v3_led_init(nocti_v3_led_t *led,
                            nocti_v3_board_t *board,
                            nocti_v3_timebase_t *timebase,
                            nocti_v3_health_t *health);
/* Backward-compatible P1/P2 name; P3 implementation initializes sync then async. */
esp_err_t nocti_v3_led_safe_init(nocti_v3_led_t *led,
                                 nocti_v3_board_t *board);
esp_err_t nocti_v3_led_bind_completion_consumer(nocti_v3_led_t *led,
                                                TaskHandle_t task);
esp_err_t nocti_v3_led_submit(nocti_v3_led_t *led,
                              const nocti_v3_led_request_t *request);
bool nocti_v3_led_take_completion(nocti_v3_led_t *led,
                                  nocti_v3_led_completion_t *out_completion);
esp_err_t nocti_v3_led_request_recovery(nocti_v3_led_t *led,
                                        esp_err_t reason);
esp_err_t nocti_v3_led_force_safe_off(nocti_v3_led_t *led);
esp_err_t nocti_v3_led_wait_idle(nocti_v3_led_t *led, uint32_t timeout_ms);
bool nocti_v3_led_is_fail_safe(const nocti_v3_led_t *led);
bool nocti_v3_led_runtime_ready(const nocti_v3_led_t *led);
uint8_t nocti_v3_led_slot_to_pca(uint8_t slot_index);
void nocti_v3_led_health_snapshot(nocti_v3_led_t *led,
                                  nocti_v3_led_health_snapshot_t *out_snapshot);
esp_err_t nocti_v3_led_reset_counters(nocti_v3_led_t *led);
esp_err_t nocti_v3_led_set_current_profile(nocti_v3_led_t *led,
                                            uint16_t current_735_ma,
                                            uint16_t current_850_ma);
void nocti_v3_led_get_current_profile(nocti_v3_led_t *led,
                                      uint16_t *current_735_ma,
                                      uint16_t *current_850_ma);
const char *nocti_v3_led_state_name(nocti_v3_led_state_t state);

#ifdef __cplusplus
}
#endif
