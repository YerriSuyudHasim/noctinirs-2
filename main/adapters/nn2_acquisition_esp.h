#ifndef NN2_ACQUISITION_ESP_H
#define NN2_ACQUISITION_ESP_H
#include "esp_err.h"
#include "nn2_core_acquisition.h"
#include "nn2_port_optical.h"
#include "nn2_port_safety.h"
typedef struct { nn2_core_adc_observation_t adc; uint32_t dropped, discarded_tail; bool stop_ack; } nn2_acquisition_status_t;
nn2_acquisition_status_t nn2_acquisition_status(void);
esp_err_t nn2_acquisition_init(void);
nn2_port_optical_t nn2_acquisition_port(void);
nn2_port_safety_t nn2_acquisition_safety_port(void);
/* LED-owner task only; value copy, nonblocking. Overflow faults acquisition. */
bool nn2_acquisition_window(const nn2_core_acq_window_t *);
#endif
