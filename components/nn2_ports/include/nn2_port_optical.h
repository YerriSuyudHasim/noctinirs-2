#ifndef NN2_PORT_OPTICAL_H
#define NN2_PORT_OPTICAL_H
#include "nn2_core_optical.h"
enum { NN2_OPTICAL_IDLE, NN2_OPTICAL_STARTING, NN2_OPTICAL_RUNNING,
       NN2_OPTICAL_STOPPING, NN2_OPTICAL_FAULT };
typedef struct {
    uint8_t fs;
    uint16_t current_ma[NN2_OPTICAL_SLOTS];
} nn2_port_optical_config_t;
/* Application-owner task only, nonblocking, never ISR/reentry. start snapshots
 * config and queues work; status reports completion. Safety hold_off must cancel
 * pending start/phase work and assert reset before returning. IDLE is reported
 * only after all device/bus handles are released. No claim of ADC acquisition. */
typedef struct {
    nn2_status_t (*start)(void *, const nn2_port_optical_config_t *);
    uint8_t (*state)(void *);
    nn2_status_t (*clear_fault)(void *);
    void *ctx;
    uint16_t current_limit_ma;
    const char *policy_info; /* Static capability text, or NULL for strict. */
} nn2_port_optical_t;
#endif
