/* Fixed-capacity sample ring buffer. Caller owns storage; no heap; not thread-safe
 * (single producer/consumer in one context; concurrency strategy is an RTOS review item). */
#ifndef NN2_CORE_RING_H
#define NN2_CORE_RING_H

#include "nn2_core_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NN2_CORE_RING_CAPACITY 16u

typedef struct {
    nn2_core_sample_t items[NN2_CORE_RING_CAPACITY];
    size_t head;   /**< next write index */
    size_t tail;   /**< next read index */
    size_t count;
    uint32_t dropped; /**< pushes rejected because full */
} nn2_core_ring_t;

nn2_status_t nn2_core_ring_init(nn2_core_ring_t *ring);
/** Push; returns NN2_ERR_FULL (and counts a drop) when full. Never overwrites. */
nn2_status_t nn2_core_ring_push(nn2_core_ring_t *ring, const nn2_core_sample_t *sample);
nn2_status_t nn2_core_ring_pop(nn2_core_ring_t *ring, nn2_core_sample_t *out);
size_t nn2_core_ring_count(const nn2_core_ring_t *ring);
bool nn2_core_ring_is_full(const nn2_core_ring_t *ring);

#ifdef __cplusplus
}
#endif
#endif /* NN2_CORE_RING_H */
