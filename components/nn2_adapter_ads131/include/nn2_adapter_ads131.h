#ifndef NN2_ADAPTER_ADS131_H
#define NN2_ADAPTER_ADS131_H

#include "nn2_ads131_logic.h"
#include "nn2_port_adc.h"
#include "nn2_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Binds the single ADS131 adapter to the port. Pins are copied.
   The caller keeps no GPIO literals inside this component. */
nn2_status_t nn2_adapter_ads131_bind(nn2_port_adc_t *port,
                                     const nn2_ads131_pins_t *pins);

/* Creates nn2_ads131 only after safe_state and init have succeeded.
   Composition must call port->safe_state before this function. */
nn2_status_t nn2_adapter_ads131_create_task(void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* NN2_ADAPTER_ADS131_H */
