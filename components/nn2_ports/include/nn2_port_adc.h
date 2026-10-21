/* Concrete API contract: docs/ISSUE13_COMPONENT_CONTRACT.md.
 * Caller-owned typed storage, exclusive task context, no ISR/reentry.
 * Value/port contracts are separate from opaque operational module handles. */
/* ADC sampler port (ADR-0001 3.6 "adc"), pull model, one conversion per call.
 * Caller context: task only. Blocking for at most one conversion.
 * Output: raw counts written to *raw_out only on NN2_OK.
 * Provenance: `source` declares what read() delivers (FAKE for disabled/fake
 * adapters, ADC for a real converter); the sequencer copies it into every sample.
 * A zero-initialised port reads NN2_SAMPLE_SRC_UNKNOWN.
 * Timing expectation (to be measured): read() < 100 us. */
#ifndef NN2_PORT_ADC_H
#define NN2_PORT_ADC_H

#include "nn2_core_types.h"

typedef struct {
    nn2_status_t (*read)(void *ctx, uint8_t channel, uint32_t *raw_out);
    void *ctx;
    nn2_sample_src_t source;
} nn2_port_adc_t;

#endif /* NN2_PORT_ADC_H */
