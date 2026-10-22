/* White-box test seam, excluded from public production include roots.
 * Snapshots are copies; these helpers never expose a writable operational alias.
 * Only boundary preconditions use private save; normal calls use production APIs. */
#ifndef NN2_TEST_PRIVATE_H
#define NN2_TEST_PRIVATE_H
#include "../../components/nn2_core/src/nn2_core_ring_internal.h"
#include "../../components/nn2_app/src/nn2_app_sequencer_internal.h"
#define DECLARE_RING(name) nn2_core_ring_storage_t name##_storage; nn2_core_ring_t *name = NULL
#define DECLARE_SEQ(name) nn2_app_seq_storage_t name##_storage; nn2_app_sequencer_t *name = NULL
static inline nn2_core_ring_state_t ring_view(const nn2_core_ring_t *ring)
{
    nn2_core_ring_state_t state; nn2_core_ring_load(ring, &state); return state;
}
static inline nn2_app_seq_internal_t seq_view(const nn2_app_sequencer_t *seq)
{
    nn2_app_seq_internal_t state; nn2_app_seq_load(seq, &state); return state;
}
#endif
