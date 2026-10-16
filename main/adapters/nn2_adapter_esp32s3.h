/* ESP32-S3 adapter constructors. Only main/ (composition root) includes this. */
#ifndef NN2_ADAPTER_ESP32S3_H
#define NN2_ADAPTER_ESP32S3_H

#include "nn2_port_led.h"
#include "nn2_port_adc.h"
#include "nn2_port_clock.h"
#include "nn2_port_log.h"

nn2_port_clock_t nn2_adapter_clock_esp_port(void);
nn2_port_log_t nn2_adapter_log_esp_port(void);
nn2_status_t nn2_adapter_adc_esp_init(nn2_port_adc_t *out);
nn2_status_t nn2_adapter_led_esp_init(nn2_port_led_t *out);

#endif
