/* Emitter safe-off port (issue #49, ADR-0001 emitter port).
 *
 * Caller context: the composition root only, inside app_main, before any
 * application task, queue, timer, or Wi-Fi is created. ISR use is forbidden.
 * Single caller. Not reentrant. The adapter must not wait on another task.
 * Each bus operation returns within the adapter's bounded timeout or
 * NN2_ERR_TIMEOUT. There is no sleep/MODE1 operation on this port.
 *
 * set_reset_asserted(true) drives RESET# active (held low).
 * set_reset_asserted(false) releases RESET#. The electrical release encoding
 * is an adapter decision and is HW-TBD (Agent 04) until specified.
 * write_* values other than the off state are rejected by the adapter.
 * Timing of the reset pulse is a requirement to be measured (Class H), not a
 * guarantee. off-state success does not prove optical darkness.
 *
 * Issue #49 register identities used by the adapter for the two named writes:
 * IREFALL is 0x40 and PWMALL is 0x3F. LEDOUT and per-channel IREF addresses
 * are not stated in issue #49, an accepted ADR, or active main config, so they
 * are not constants of this header.
 */
#ifndef NN2_PORT_EMITTER_SAFE_H
#define NN2_PORT_EMITTER_SAFE_H

#include "nn2_core_types.h"

#define NN2_PORT_EMITTER_SAFE_REG_IREFALL 0x40u
#define NN2_PORT_EMITTER_SAFE_REG_PWMALL 0x3Fu
#define NN2_PORT_EMITTER_SAFE_REG_OFF 0x00u
#define NN2_PORT_EMITTER_SAFE_LEDOUT_COUNT 6u
#define NN2_PORT_EMITTER_SAFE_IREF_COUNT 24u

typedef struct {
    nn2_status_t (*set_reset_asserted)(void *ctx, bool asserted);
    nn2_status_t (*write_irefall)(void *ctx, uint8_t value);
    nn2_status_t (*write_pwmall)(void *ctx, uint8_t value);
    nn2_status_t (*write_ledout)(void *ctx, uint8_t index, uint8_t value);
    nn2_status_t (*read_iref)(void *ctx, uint8_t index, uint8_t *value);
    nn2_status_t (*read_ledout)(void *ctx, uint8_t index, uint8_t *value);
    /* Bounded wait. ms is the request. The adapter must not use a scheduler
     * delay. A failure returns an error and must not retry without bound. */
    nn2_status_t (*delay_ms)(void *ctx, uint32_t ms);
    void *ctx;
} nn2_port_emitter_safe_t;

#endif /* NN2_PORT_EMITTER_SAFE_H */
