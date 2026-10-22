/* PCA9956B safe-off adapter. Hardware parameters that issue #49 does not
 * state live in nn2_emitter_safe_hw.h and fail the target build.
 * bind() does not touch a pin or the bus. */
#ifndef NN2_ADAPTER_EMITTER_SAFE_H
#define NN2_ADAPTER_EMITTER_SAFE_H

#include "nn2_port_emitter_safe.h"

nn2_status_t nn2_adapter_emitter_safe_bind(nn2_port_emitter_safe_t *out);

#endif
