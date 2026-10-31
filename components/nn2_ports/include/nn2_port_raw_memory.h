#ifndef NN2_PORT_RAW_MEMORY_H
#define NN2_PORT_RAW_MEMORY_H
#include "nn2_core_raw_buffer.h"
#include "nn2_core_types.h"
/* STOP-only RAW ownership transition, called by the application owner task.
 * allocate/release never run from acquisition, an ISR, START or STOP itself.
 * bind(NULL) must complete consumer detachment before release is permitted. */
typedef struct {
    void *(*allocate)(void *, size_t);
    void (*release)(void *, void *);
    nn2_status_t (*bind)(void *, nn2_raw_buffer_t *);
    void *ctx;
} nn2_port_raw_memory_t;
#endif
