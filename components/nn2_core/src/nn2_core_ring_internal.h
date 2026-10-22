/* Private representation; production callers must not include this header.
 * Test-only white-box snapshots use these same memcpy helpers, not type punning. */
#ifndef NN2_CORE_RING_INTERNAL_H
#define NN2_CORE_RING_INTERNAL_H
#include "nn2_core_ring.h"
#include <string.h>
/* Handle pointer is an aligned identity token. Never dereference it as this type.
 * Conversion back to the original storage pointer precedes every object access. */
struct nn2_core_ring { max_align_t alignment; };
typedef struct {
    nn2_core_sample_t items[NN2_CORE_RING_CAPACITY];
    size_t head, tail, count;
    uint32_t dropped;
} nn2_core_ring_state_t;
_Static_assert(sizeof(nn2_core_ring_state_t) <= NN2_CORE_RING_STORAGE_SIZE, "ring storage too small");
_Static_assert(_Alignof(nn2_core_ring_storage_t) >= _Alignof(nn2_core_ring_t), "ring handle alignment");
static inline void nn2_core_ring_load(const nn2_core_ring_t *handle, nn2_core_ring_state_t *state)
{
    const nn2_core_ring_storage_t *storage = (const nn2_core_ring_storage_t *)(const void *)handle;
    memcpy(state, storage->opaque, sizeof(*state));
}
static inline void nn2_core_ring_save(nn2_core_ring_t *handle, const nn2_core_ring_state_t *state)
{
    nn2_core_ring_storage_t *storage = (nn2_core_ring_storage_t *)(void *)handle;
    memcpy(storage->opaque, state, sizeof(*state));
}
#endif
