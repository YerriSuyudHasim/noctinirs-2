/* Concrete API contract: docs/ISSUE13_COMPONENT_CONTRACT.md.
 * Caller-owned typed storage, exclusive task context, no ISR/reentry.
 * Opaque state; caller storage holds bytes copied to/from typed private objects. */
/* Acquisition sequencer: IDLE -> LED_ON -> SAMPLE -> LED_OFF -> IDLE.
 * Ports are injected; no platform includes. Single-context use only.
 * On any port error the sequencer requests LED off (fail-safe) and enters FAULT;
 * nn2_app_sequencer_reset() calls led.off() and returns to IDLE only if off()
 * succeeds (then last_error is cleared); otherwise it returns the off() error
 * and stays in FAULT.
 * Caller context: one owner task, never ISR. All callbacks execute synchronously;
 * finite completion/bounds depend on their contracts, not state-machine timeouts.
 * settle_us is a delay request, not an enforced wall-clock bound. Timing is unmeasured.
 * Cleanup (fail-safe off) runs synchronously in the caller's task. */
#ifndef NN2_APP_SEQUENCER_H
#define NN2_APP_SEQUENCER_H

#include "nn2_core_types.h"
#include "nn2_core_ring.h"
#include "nn2_port_led.h"
#include "nn2_port_adc.h"
#include "nn2_port_clock.h"
#include "nn2_port_log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NN2_APP_SEQ_IDLE = 0,
    NN2_APP_SEQ_LED_ON,
    NN2_APP_SEQ_SAMPLE,
    NN2_APP_SEQ_LED_OFF,
    NN2_APP_SEQ_FAULT
} nn2_app_seq_state_t;

typedef struct {
    nn2_port_led_t led;
    nn2_port_adc_t adc;
    nn2_port_clock_t clock;
    nn2_port_log_t log;      /**< write may be NULL */
    nn2_core_ring_t *ring;   /**< borrowed, owned by caller, must outlive sequencer */
    uint8_t channel;
    uint32_t settle_us;      /**< wait between LED on and sample (clock.delay_us) */
} nn2_app_seq_config_t;

#define NN2_APP_SEQ_STORAGE_SIZE 512u
typedef struct nn2_app_sequencer nn2_app_sequencer_t;
typedef union {
    max_align_t alignment;
    unsigned char opaque[NN2_APP_SEQ_STORAGE_SIZE];
} nn2_app_seq_storage_t;
/* Copied observations, never a view/alias of mutable operational state. */
typedef struct {
    nn2_app_seq_state_t state;
    uint32_t next_seq, cycles_ok, faults, off_failures;
    nn2_status_t last_error;
} nn2_app_seq_observation_t;
/* Init has no hardware side effects; reinit is not a FAULT-recovery operation. */
nn2_status_t nn2_app_sequencer_init(nn2_app_seq_storage_t *storage,
    nn2_app_sequencer_t **out, const nn2_app_seq_config_t *cfg);
nn2_status_t nn2_app_sequencer_observe(const nn2_app_sequencer_t *seq,
    nn2_app_seq_observation_t *out);
/** Advance exactly one state transition. Returns status of that transition. */
nn2_status_t nn2_app_sequencer_step(nn2_app_sequencer_t *seq);
/** Run steps from IDLE until IDLE (success) or FAULT. */
nn2_status_t nn2_app_sequencer_run_cycle(nn2_app_sequencer_t *seq);
nn2_status_t nn2_app_sequencer_reset(nn2_app_sequencer_t *seq);
nn2_app_seq_state_t nn2_app_sequencer_state(const nn2_app_sequencer_t *seq);
const char *nn2_app_seq_state_str(nn2_app_seq_state_t state);

#ifdef __cplusplus
}
#endif
#endif /* NN2_APP_SEQUENCER_H */
