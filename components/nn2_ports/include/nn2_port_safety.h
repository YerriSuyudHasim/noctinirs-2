#ifndef NN2_PORT_SAFETY_H
#define NN2_PORT_SAFETY_H

#include "nn2_core_types.h"

/* Single owner task; callback must not block, allocate or call back into App.
 * Success reports a software action, never proof of physical emitter state. */
typedef struct {
    void *ctx;
    nn2_status_t (*hold_off)(void *ctx);
} nn2_port_safety_t;

#endif
