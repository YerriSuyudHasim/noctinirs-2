#ifndef NN2_LED_BRINGUP_H
#define NN2_LED_BRINGUP_H
#include "esp_err.h"
#include "nn2_port_optical.h"
#include "nn2_port_safety.h"
#include "nn2_board_pca.h"
typedef struct {
    uint8_t state, pca_mode2, i2c_event;
    uint32_t frames, edges, deadline_misses, transfer_max_us;
    int32_t error;
    uint32_t stack_free, dispatch_max_us, completion_max_us;
    uint32_t i2c_submitted, i2c_completed, i2c_errors, i2c_stale;
    uint8_t first_stop;
    /* Compatibility telemetry: cap stays false; attempt marks this run, not a lock. */
    bool limit_hit, attempt_used;
    bool bus_released, reboot_required;
    nn2_board_pca_diagnostics_t pca, last_complete, first_fault;
} nn2_led_bringup_status_t;
esp_err_t nn2_led_bringup_init(void);
nn2_port_optical_t nn2_led_bringup_port(void);
nn2_port_safety_t nn2_led_bringup_safety_port(void);
nn2_led_bringup_status_t nn2_led_bringup_status(void);
/* Pure cached serializer; false indicates truncation, never a clean snapshot. */
bool nn2_led_bringup_format_status(char *, size_t, const nn2_led_bringup_status_t *);
#endif
