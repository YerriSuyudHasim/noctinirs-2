/* Acquisition sequencer: IDLE -> LED_ON -> SAMPLE -> LED_OFF -> IDLE.
 * Ports are injected; no platform includes. Single-context use only.
 * On any port error the sequencer requests LED off (fail-safe) and enters FAULT;
 * nn2_app_sequencer_reset() calls led.off() and returns to IDLE only if off()
 * succeeds (then last_error is cleared); otherwise it returns the off() error
 * and stays in FAULT.
 * Caller context: one owner task, never ISR. The sequencer itself never blocks
 * except through clock.delay_us(settle_us), which is bounded by settle_us.
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

typedef struct {
    nn2_app_seq_config_t cfg;
    nn2_app_seq_state_t state;
    nn2_core_sample_t pending;
    uint32_t next_seq;
    uint32_t cycles_ok;
    uint32_t faults;
    uint32_t off_failures;  /**< failed fail-safe/reset off() calls */
    nn2_status_t last_error;
} nn2_app_sequencer_t;

nn2_status_t nn2_app_sequencer_init(nn2_app_sequencer_t *seq, const nn2_app_seq_config_t *cfg);
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
