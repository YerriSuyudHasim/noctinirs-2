#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nocti_v4_board.h"
#include "nocti_v4_health.h"
#include "nocti_v4_i2c.h"
#include "nocti_v4_led.h"
#include "nocti_v4_time.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PCA_IO_MODE_SYNC = 0,
    PCA_IO_MODE_ENTERING,
    PCA_IO_MODE_ASYNC,
    PCA_IO_MODE_ASYNC_FAULTED,
    PCA_IO_MODE_EXITING,
} nocti_v4_pca_io_mode_t;

typedef enum {
    NOCTI_V4_ASYNC_PAYLOAD_NONE = 0,
    NOCTI_V4_ASYNC_PAYLOAD_SINGLE_ON,
    NOCTI_V4_ASYNC_PAYLOAD_SINGLE_OFF,
    NOCTI_V4_ASYNC_PAYLOAD_BURST_ON,
    NOCTI_V4_ASYNC_PAYLOAD_BURST_OFF,
} nocti_v4_async_payload_type_t;

typedef struct {
    bool initialized;
    bool callbacks_registered;
    bool transaction_pending;
    bool submissions_allowed;
    nocti_v4_pca_io_mode_t mode;
    uint32_t i2c_clock_hz;
    uint32_t i2c_queue_depth;
    uint32_t generation;
    uint32_t next_command_id;
    int32_t selected_source;
    int32_t task_core;
    int32_t callback_core;
    int32_t init_core;
    esp_err_t last_result;
    uint32_t last_command_id;
    uint8_t last_source;
    uint8_t last_channel;
    nocti_v4_async_payload_type_t last_payload_type;
    uint8_t last_payload[7];
    uint8_t last_payload_length;
    uint64_t last_deadline_timestamp_us;
    uint64_t last_request_timestamp_us;
    uint64_t last_queue_timestamp_us;
    uint64_t last_queue_exit_timestamp_us;
    uint64_t last_dequeue_timestamp_us;
    uint64_t last_submit_timestamp_us;
    uint64_t last_i2c_submit_return_timestamp_us;
    uint64_t last_complete_timestamp_us;
    uint32_t last_deadline_to_request_us;
    uint32_t last_request_to_queue_us;
    uint32_t last_deadline_to_queue_us;
    uint32_t last_queue_send_call_us;
    uint32_t last_queue_to_dequeue_us;
    uint32_t last_dequeue_to_submit_us;
    uint32_t last_i2c_submit_call_us;
    uint32_t last_request_to_complete_us;
    uint32_t last_deadline_to_complete_us;
    uint32_t last_latency_us;
    int32_t last_i2c_event;
    int32_t last_callback_core;
} nocti_v4_async_led_status_t;

/* Immutable LED-edge completion transferred from the LED owner to the
 * scheduler.  The I2C ISR still writes only the private compact ISR ring; the
 * LED task validates that record and publishes this task-context record after
 * the transaction has reached a terminal result. */
typedef struct {
    esp_err_t result;
    uint32_t command_id;
    uint32_t generation;
    uint32_t session_epoch;
    uint32_t frame_sequence;
    uint32_t slot_epoch;
    uint8_t slot_index;
    uint8_t source;
    uint8_t channel;
    uint8_t payload_length;
    nocti_v4_async_payload_type_t payload_type;
    uint64_t deadline_timestamp_us;
    uint64_t request_timestamp_us;
    uint64_t queue_timestamp_us;
    uint64_t dequeue_timestamp_us;
    uint64_t submit_timestamp_us;
    uint64_t complete_timestamp_us;
    int32_t i2c_event;
    int32_t callback_core;
} nocti_v4_async_led_completion_t;

typedef struct {
    void *runtime;
    bool initialized;
} nocti_v4_async_led_t;

/* Creates a dormant static LED-control task. The PCA stays qdepth=0 SYNC until
 * ASYNC_ENTER is explicitly accepted. */
esp_err_t nocti_v4_async_led_init(nocti_v4_async_led_t *async_led,
                                  nocti_v4_board_t *board,
                                  nocti_v4_i2c_t *i2c,
                                  nocti_v4_led_t *led,
                                  nocti_v4_time_t *timebase,
                                  nocti_v4_health_t *health);
esp_err_t nocti_v4_async_led_enter(nocti_v4_async_led_t *async_led);
esp_err_t nocti_v4_async_led_exit(nocti_v4_async_led_t *async_led);
esp_err_t nocti_v4_async_led_on_single(nocti_v4_async_led_t *async_led,
                                        uint8_t source);
esp_err_t nocti_v4_async_led_off_single(nocti_v4_async_led_t *async_led);
esp_err_t nocti_v4_async_led_on_burst(nocti_v4_async_led_t *async_led,
                                       uint8_t source);
esp_err_t nocti_v4_async_led_all_off_burst(nocti_v4_async_led_t *async_led);
/* Scheduler-only timed variants. request_timestamp_us is captured when the
 * immutable slot request is created; the LED owner captures queue, dequeue,
 * driver-submit, and callback completion boundaries in the same time domain. */
esp_err_t nocti_v4_async_led_on_burst_timed(nocti_v4_async_led_t *async_led,
                                             uint8_t source,
                                             uint64_t request_timestamp_us);
esp_err_t nocti_v4_async_led_all_off_burst_timed(nocti_v4_async_led_t *async_led,
                                                  uint64_t request_timestamp_us);
/* Bind the scheduler task as the sole consumer of SCAN-edge completions. This
 * is performed once while the PCA owner is in synchronous/idle mode. */
esp_err_t nocti_v4_async_led_bind_scheduled_consumer(
    nocti_v4_async_led_t *async_led, TaskHandle_t scheduler_task);
/* SCAN-only critical-path entry points. The scheduler is the sole producer.
 * They copy one immutable command with zero wait and return after handoff;
 * they do not wait for I2C completion. The LED task publishes the terminal
 * result through nocti_v4_async_led_pop_scheduled_completion(). */
esp_err_t nocti_v4_async_led_on_burst_scheduled(
    nocti_v4_async_led_t *async_led, uint8_t source,
    uint32_t session_epoch, uint32_t frame_sequence, uint32_t slot_epoch,
    uint8_t slot_index, uint64_t deadline_timestamp_us,
    uint64_t request_timestamp_us);
/* Normal SCAN edge only. This requires the currently accepted state to be the
 * matching source written by a verified seven-byte one-hot ON command. It is
 * never a replacement for the seven-byte ALL_OFF used by STOP/fault/recovery. */
esp_err_t nocti_v4_async_led_off_single_scheduled(
    nocti_v4_async_led_t *async_led, uint8_t source,
    uint32_t session_epoch, uint32_t frame_sequence, uint32_t slot_epoch,
    uint8_t slot_index, uint64_t deadline_timestamp_us,
    uint64_t request_timestamp_us);
esp_err_t nocti_v4_async_led_all_off_burst_scheduled(
    nocti_v4_async_led_t *async_led, uint64_t deadline_timestamp_us,
    uint64_t request_timestamp_us);
bool nocti_v4_async_led_pop_scheduled_completion(
    nocti_v4_async_led_t *async_led,
    nocti_v4_async_led_completion_t *out_completion);
bool nocti_v4_async_led_take_scheduled_overflow(nocti_v4_async_led_t *async_led);
bool nocti_v4_async_led_is_active(const nocti_v4_async_led_t *async_led);
nocti_v4_pca_io_mode_t nocti_v4_async_led_mode(const nocti_v4_async_led_t *async_led);
void nocti_v4_async_led_get_status(const nocti_v4_async_led_t *async_led,
                                   nocti_v4_async_led_status_t *out_status);
void nocti_v4_async_led_print_status(const nocti_v4_async_led_t *async_led);
void nocti_v4_async_led_print_health(const nocti_v4_async_led_t *async_led);
void nocti_v4_async_led_print_timing(const nocti_v4_async_led_t *async_led);
void nocti_v4_async_led_print_timing_detail(const nocti_v4_async_led_t *async_led);
void nocti_v4_async_led_print_last_result(const nocti_v4_async_led_t *async_led);
/* Prints the immutable source->one-hot seven-byte payload table. This is
 * read-only and remains usable while the normal qdepth=0 owner is active. */
void nocti_v4_async_led_print_payloads(const nocti_v4_async_led_t *async_led);
/* Prints the immutable source->two-byte normal-SCAN OFF table. */
void nocti_v4_async_led_print_off_payloads(const nocti_v4_async_led_t *async_led);
/* Console/status diagnostic only; never call from a PCA/I2C/ADS hot path. */
void nocti_v4_async_led_sample_stack_watermark(nocti_v4_async_led_t *async_led);
const char *nocti_v4_pca_io_mode_name(nocti_v4_pca_io_mode_t mode);
const char *nocti_v4_async_payload_type_name(nocti_v4_async_payload_type_t type);

#ifdef __cplusplus
}
#endif

/* Nonblocking NN2 safety adapter hook: refuse subsequent normal submissions.
 * Caller already holds emitter RESET low; the owner still drains/reclaims. */
void nocti_v4_async_led_cancel_submissions(nocti_v4_async_led_t *async_led);
