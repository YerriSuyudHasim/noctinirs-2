#include "nn2_core_ring.h"
#include <string.h>

nn2_status_t nn2_core_ring_init(nn2_core_ring_t *ring)
{
    if (ring == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    memset(ring, 0, sizeof(*ring));
    return NN2_OK;
}

nn2_status_t nn2_core_ring_push(nn2_core_ring_t *ring, const nn2_core_sample_t *sample)
{
    if (ring == NULL || sample == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    if (ring->count >= NN2_CORE_RING_CAPACITY) {
        ring->dropped++;
        return NN2_ERR_FULL;
    }
    ring->items[ring->head] = *sample;
    ring->head = (ring->head + 1u) % NN2_CORE_RING_CAPACITY;
    ring->count++;
    return NN2_OK;
}

nn2_status_t nn2_core_ring_pop(nn2_core_ring_t *ring, nn2_core_sample_t *out)
{
    if (ring == NULL || out == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    if (ring->count == 0u) {
        return NN2_ERR_EMPTY;
    }
    *out = ring->items[ring->tail];
    ring->tail = (ring->tail + 1u) % NN2_CORE_RING_CAPACITY;
    ring->count--;
    return NN2_OK;
}

size_t nn2_core_ring_count(const nn2_core_ring_t *ring)
{
    return (ring == NULL) ? 0u : ring->count;
}

bool nn2_core_ring_is_full(const nn2_core_ring_t *ring)
{
    return (ring != NULL) && (ring->count >= NN2_CORE_RING_CAPACITY);
}
