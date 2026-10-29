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
 * only after all device/bus handles are released. An acquisition wrapper also
 * requires acknowledged ADC stop before exposing IDLE. */
typedef struct {
    nn2_status_t (*start)(void *, const nn2_port_optical_config_t *);
    uint8_t (*state)(void *);
    nn2_status_t (*clear_fault)(void *);
    void *ctx;
    uint16_t current_limit_ma;
    /* Optional acquisition consumer: one Application task, nonblocking pop.
     * Copies a FRAME payload; EMPTY leaves output unchanged. real_samples
     * changes provenance only for this explicit, complete ADC+LED binding. */
    nn2_status_t (*read_frame)(void *, uint8_t *, size_t, uint64_t *timestamp);
    bool real_samples;
    const char *policy_info; /* Static capability text, or NULL for strict. */
    /* Optional read-only diagnostic. kind 0 is health, kind 1 is timing.
     * Called from the UDP owner task, never from an ISR or the ADC hot path.
     * Writes ASCII without a trailing NUL requirement and returns the length.
     * NULL or 0 leaves an unavailable report. Not an ACQ1 frame. */
    size_t (*read_diag)(void *, uint8_t kind, uint8_t *out, size_t capacity);
} nn2_port_optical_t;
#endif
