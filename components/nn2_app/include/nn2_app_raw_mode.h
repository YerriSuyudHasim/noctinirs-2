#ifndef NN2_APP_RAW_MODE_H
#define NN2_APP_RAW_MODE_H
#include "nn2_port_raw_memory.h"
/* Caller has confirmed STOPPED/quiescent. Failure preserves the prior pointer.
 * NN2_ERR_FULL specifically means the requested RAW allocation failed. */
nn2_status_t nn2_app_raw_mode_set(nn2_raw_buffer_t **,
    const nn2_port_raw_memory_t *, bool engineering);
#endif
