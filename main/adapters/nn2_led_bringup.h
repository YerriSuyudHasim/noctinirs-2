#ifndef NN2_LED_BRINGUP_H
#define NN2_LED_BRINGUP_H
#include "esp_err.h"
#include "nn2_port_optical.h"
#include "nn2_port_safety.h"
typedef struct {
    uint8_t state, pca_mode2;
    uint32_t frames, edges, deadline_misses, transfer_max_us;
    int32_t error;
    uint32_t stack_free;
    bool bus_released;
} nn2_led_bringup_status_t;
esp_err_t nn2_led_bringup_init(void);
nn2_port_optical_t nn2_led_bringup_port(void);
nn2_port_safety_t nn2_led_bringup_safety_port(void);
nn2_led_bringup_status_t nn2_led_bringup_status(void);
#endif
