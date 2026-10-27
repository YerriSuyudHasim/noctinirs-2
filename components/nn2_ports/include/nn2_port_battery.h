#ifndef NN2_PORT_BATTERY_H
#define NN2_PORT_BATTERY_H

#include "nn2_core_types.h"

typedef struct {
    uint16_t vcell_raw;
    uint16_t soc_raw;
    uint32_t voltage_uv;
    uint32_t soc_millipercent;
    uint8_t bq_status_raw;
} nn2_port_battery_sample_t;

/* Single task, no reentry/ISR. Callback publishes only a complete sample.
 * May block for bounded I/O; call only in an exclusive, sufficiently long idle
 * window on the shared bus. Success guarantees bus resources were released.
 * After an error, the owner must confirm cleanup before granting another client
 * the bus; resource cleanup failure must not be treated as an idle window. */
typedef struct {
    void *ctx;
    nn2_status_t (*read)(void *ctx, nn2_port_battery_sample_t *sample);
} nn2_port_battery_t;

#endif
