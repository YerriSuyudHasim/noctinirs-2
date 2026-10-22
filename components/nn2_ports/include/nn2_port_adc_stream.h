/* Proposed temporary compatibility port; scalar demo ABI is unchanged.
 * Contract/review status: docs/ISSUE35_DIAGNOSTIC_CONTRACT.md.
 * One serialized task caller; no ISR/reentry. Static ctx lives until reboot.
 * Every operation blocks up to timeout_ms (1..1000), conditionally on RTOS
 * scheduling. TIMEOUT does not acknowledge quiescence. No retained pointers.
 * Output copies remain unchanged on failure; EMPTY means no record by the read deadline.
 * INVALID_ARG: null/bad deadline; INVALID_STATE: lifecycle; IO: device fault.
 * init: STOPPED/READY/FAULT -> READY, flush/count old queue; start: READY -> RUNNING;
 * stop: any -> STOPPED with reset held; snapshot/read do not alter device config.
 * SPI completion timeout requires reboot; stop never frees in-flight buffers. */
#ifndef NN2_PORT_ADC_STREAM_H
#define NN2_PORT_ADC_STREAM_H
#include "nn2_core_adc_record.h"
typedef struct {
    void *ctx;
    nn2_status_t (*init)(void *ctx, uint32_t timeout_ms);
    nn2_status_t (*start)(void *ctx, uint32_t timeout_ms);
    nn2_status_t (*stop)(void *ctx, uint32_t timeout_ms);
    nn2_status_t (*read)(void *ctx, nn2_core_adc_record_t *out, uint32_t timeout_ms);
    nn2_status_t (*observe)(void *ctx, nn2_core_adc_observation_t *out, uint32_t timeout_ms);
} nn2_port_adc_stream_t;
#endif
