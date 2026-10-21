/* Concrete API contract: docs/ISSUE13_COMPONENT_CONTRACT.md.
 * Caller-owned typed storage, exclusive task context, no ISR/reentry.
 * Public state is observational; opaque-handle assessment remains pending. */
/* Clock / timebase port (ADR-0001 3.6 "clock").
 * now_us(): monotonic 64-bit microseconds, any task context, nonblocking.
 * delay_us(): busy/short wait, task context only; may be NULL (no settle delay).
 * delay_us has no status/error/timeout return; no settle fault is observable.
 * Resolution and cross-core consistency are to be measured (Class H). */
#ifndef NN2_PORT_CLOCK_H
#define NN2_PORT_CLOCK_H

#include "nn2_core_types.h"

typedef struct {
    uint64_t (*now_us)(void *ctx);
    void (*delay_us)(void *ctx, uint32_t us);
    void *ctx;
} nn2_port_clock_t;

#endif /* NN2_PORT_CLOCK_H */
