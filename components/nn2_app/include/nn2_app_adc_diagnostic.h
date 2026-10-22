#ifndef NN2_APP_ADC_DIAGNOSTIC_H
#define NN2_APP_ADC_DIAGNOSTIC_H
#include "nn2_port_adc_stream.h"
/* Single task, no reentry/ISR. Bounded operation count, each call <=1000ms.
 * Copies final observation on OK only. Always attempts stop, even init failure.
 * No retries/restart, no emitter or transport dependency. */
nn2_status_t nn2_app_adc_diagnostic_run(const nn2_port_adc_stream_t *port,
                                      uint32_t attempts,
                                      nn2_core_adc_observation_t *out, nn2_core_adc_record_t *last);
#endif
