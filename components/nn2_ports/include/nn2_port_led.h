/* LED / emitter port (ADR-0001 3.6 "emitter").
 * Caller context: task only (not ISR). Nonblocking. Not reentrant per instance.
 * Fail-safe: implementations must treat off() as always safe to call, including
 * after a failed on(). Drive limits/pins are adapter configuration, never Core/App.
 * Timing expectation (to be measured, not guaranteed): on()/off() return < 50 us. */
#ifndef NN2_PORT_LED_H
#define NN2_PORT_LED_H

#include "nn2_core_types.h"

typedef struct {
    nn2_status_t (*on)(void *ctx, uint8_t channel);
    nn2_status_t (*off)(void *ctx, uint8_t channel);
    void *ctx;
} nn2_port_led_t;

#endif /* NN2_PORT_LED_H */
