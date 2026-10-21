#include "nn2_core_ring_internal.h"

nn2_status_t nn2_core_ring_init(nn2_core_ring_storage_t *storage, nn2_core_ring_t **out)
{
    if (storage == NULL || out == NULL) return NN2_ERR_INVALID_ARG;
    nn2_core_ring_state_t state = {0};
    nn2_core_ring_t *handle = (nn2_core_ring_t *)(void *)storage;
    memset(storage->opaque, 0, sizeof(storage->opaque));
    nn2_core_ring_save(handle, &state);
    *out = handle;
    return NN2_OK;
}

nn2_status_t nn2_core_ring_push(nn2_core_ring_t *ring, const nn2_core_sample_t *sample)
{
    if (ring == NULL || sample == NULL) return NN2_ERR_INVALID_ARG;
    nn2_core_ring_state_t state;
    nn2_core_ring_load(ring, &state);
    if (state.count >= NN2_CORE_RING_CAPACITY) {
        state.dropped++;
        nn2_core_ring_save(ring, &state);
        return NN2_ERR_FULL;
    }
    state.items[state.head] = *sample;
    state.head = (state.head + 1u) % NN2_CORE_RING_CAPACITY;
    state.count++;
    nn2_core_ring_save(ring, &state);
    return NN2_OK;
}

nn2_status_t nn2_core_ring_pop(nn2_core_ring_t *ring, nn2_core_sample_t *out)
{
    if (ring == NULL || out == NULL) return NN2_ERR_INVALID_ARG;
    nn2_core_ring_state_t state;
    nn2_core_ring_load(ring, &state);
    if (state.count == 0u) return NN2_ERR_EMPTY;
    *out = state.items[state.tail];
    state.tail = (state.tail + 1u) % NN2_CORE_RING_CAPACITY;
    state.count--;
    nn2_core_ring_save(ring, &state);
    return NN2_OK;
}

size_t nn2_core_ring_count(const nn2_core_ring_t *ring)
{
    if (ring == NULL) return 0u;
    nn2_core_ring_state_t state;
    nn2_core_ring_load(ring, &state);
    return state.count;
}

bool nn2_core_ring_is_full(const nn2_core_ring_t *ring)
{
    return nn2_core_ring_count(ring) >= NN2_CORE_RING_CAPACITY;
}

uint32_t nn2_core_ring_dropped(const nn2_core_ring_t *ring)
{
    if (ring == NULL) return 0u;
    nn2_core_ring_state_t state;
    nn2_core_ring_load(ring, &state);
    return state.dropped;
}
