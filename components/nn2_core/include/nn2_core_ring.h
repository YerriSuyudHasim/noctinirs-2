/* Opaque FIFO. Caller owns typed storage; no heap, ISR or callback reentry.
 * See docs/ISSUE13_COMPONENT_CONTRACT.md for storage/lifetime restrictions. */
#ifndef NN2_CORE_RING_H
#define NN2_CORE_RING_H
#include "nn2_core_types.h"
#ifdef __cplusplus
extern "C" {
#endif
#define NN2_CORE_RING_CAPACITY 16u
#define NN2_CORE_RING_STORAGE_SIZE 1024u
typedef struct nn2_core_ring nn2_core_ring_t;
/* Inert object-representation storage, NOT an internal struct or stable layout ABI.
 * Do not inspect/copy/move/modify it while a handle is live. */
typedef union {
    max_align_t alignment;
    unsigned char opaque[NN2_CORE_RING_STORAGE_SIZE];
} nn2_core_ring_storage_t;
/* Init/reinit writes only on success and leaves *out unchanged on error.
 * Reinit requires exclusive quiescent ownership and invalidates prior aliases. */
nn2_status_t nn2_core_ring_init(nn2_core_ring_storage_t *storage, nn2_core_ring_t **out);
/* FULL changes only the modulo-2^32 drop count. Never overwrites a record. */
nn2_status_t nn2_core_ring_push(nn2_core_ring_t *ring, const nn2_core_sample_t *sample);
/* Output is unchanged on EMPTY/INVALID_ARG. Arguments may not alias storage. */
nn2_status_t nn2_core_ring_pop(nn2_core_ring_t *ring, nn2_core_sample_t *out);
size_t nn2_core_ring_count(const nn2_core_ring_t *ring);
bool nn2_core_ring_is_full(const nn2_core_ring_t *ring);
uint32_t nn2_core_ring_dropped(const nn2_core_ring_t *ring);
#ifdef __cplusplus
}
#endif
#endif
