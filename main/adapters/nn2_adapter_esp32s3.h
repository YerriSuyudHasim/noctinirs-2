/* ESP32-S3 adapter constructors. Only main/ (composition root) includes this. */
#ifndef NN2_ADAPTER_ESP32S3_H
#define NN2_ADAPTER_ESP32S3_H

#include "nn2_port_led.h"
#include "nn2_port_adc.h"
#include "nn2_port_clock.h"
#include "nn2_port_log.h"

nn2_port_clock_t nn2_adapter_clock_esp_port(void);
nn2_port_log_t nn2_adapter_log_esp_port(void);
/* 0.1.x: only fake/disabled hardware adapters exist (PR #15, F1).
 * Real emitter/ADC adapters are deferred behind a hardware contract issue. */
nn2_status_t nn2_adapter_adc_fake_init(nn2_port_adc_t *out);      /* src=FAKE, raw=0 */
nn2_status_t nn2_adapter_led_disabled_init(nn2_port_led_t *out);  /* no-op, no GPIO */

#endif
