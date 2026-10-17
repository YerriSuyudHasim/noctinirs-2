#include "nn2_frame_ring.h"

nn2_status_t nn2_frame_ring_init(nn2_frame_ring_t *ring,
                                 nn2_adc_raw_frame_t *storage,
                                 uint16_t capacity)
{
    if (ring == 0 || storage == 0 || capacity == 0u) {
        return NN2_ERR_INVALID_ARG;
    }
    ring->storage = storage;
    ring->capacity = capacity;
    ring->head = 0u;
    ring->count = 0u;
    ring->dropped_oldest = 0u;
    return NN2_OK;
}

nn2_status_t nn2_frame_ring_push(nn2_frame_ring_t *ring,
                                 const nn2_adc_raw_frame_t *frame)
{
    nn2_status_t status = NN2_OK;
    uint16_t index;

    if (ring == 0 || frame == 0 || ring->storage == 0 || ring->capacity == 0u) {
        return NN2_ERR_INVALID_ARG;
    }
    if (ring->count == ring->capacity) {
        ring->head = (uint16_t)((ring->head + 1u) % ring->capacity);
        ring->count--;
        ring->dropped_oldest++;
        status = NN2_ERR_OVERFLOW;
    }
    index = (uint16_t)((ring->head + ring->count) % ring->capacity);
    ring->storage[index] = *frame;
    ring->count++;
    return status;
}

nn2_status_t nn2_frame_ring_pop(nn2_frame_ring_t *ring, nn2_adc_raw_frame_t *out)
{
    if (ring == 0 || out == 0 || ring->storage == 0) {
        return NN2_ERR_INVALID_ARG;
    }
    if (ring->count == 0u) {
        return NN2_ERR_MISSING;
    }
    *out = ring->storage[ring->head];
    ring->head = (uint16_t)((ring->head + 1u) % ring->capacity);
    ring->count--;
    return NN2_OK;
}
