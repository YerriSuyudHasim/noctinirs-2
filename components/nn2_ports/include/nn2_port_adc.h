#ifndef NN2_PORT_ADC_H
#define NN2_PORT_ADC_H

#include <stdint.h>

#include "nn2_adc_frame.h"
#include "nn2_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Caller-supplied acquisition settings. OSR and data rate are inputs.
   The driver does not substitute a rate of its own. ADR-0002 alignment
   of those two fields is OPEN. */
typedef struct {
    uint32_t spi_hz;
    uint32_t osr;
    uint32_t data_rate_hz;
    nn2_adc_crc_kind_t crc_kind;
    uint16_t ring_depth; /* 0 selects the adapter default */
} nn2_adc_config_t;

typedef struct {
    uint32_t crc_bad;
    uint32_t spi_errors;
    uint32_t drdy_timeouts;
    uint32_t ring_dropped_oldest;
    uint32_t frames_produced;
} nn2_adc_counters_t;

/* ADC port. No ESP-IDF or FreeRTOS types.
   Every operation is task context. None may be called from an ISR.
   safe_state, init, start, and stop are nonblocking apart from the
   adapter's bounded reset delay inside start.
   read_raw is nonblocking: it pops one queued frame or returns
   NN2_ERR_MISSING. The DRDY wait lives in the adapter task. */
typedef struct nn2_port_adc {
    void *ctx;
    nn2_status_t (*safe_state)(void *ctx);
    nn2_status_t (*init)(void *ctx, const nn2_adc_config_t *cfg);
    nn2_status_t (*start)(void *ctx);
    nn2_status_t (*stop)(void *ctx);
    nn2_status_t (*read_raw)(void *ctx, nn2_adc_raw_frame_t *out);
    nn2_status_t (*get_counters)(void *ctx, nn2_adc_counters_t *out);
} nn2_port_adc_t;

#ifdef __cplusplus
}
#endif

#endif /* NN2_PORT_ADC_H */
