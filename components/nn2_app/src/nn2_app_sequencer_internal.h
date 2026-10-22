/* Private representation and memcpy transfer; no public writable state view. */
#ifndef NN2_APP_SEQUENCER_INTERNAL_H
#define NN2_APP_SEQUENCER_INTERNAL_H
#include "nn2_app_sequencer.h"
#include <string.h>
struct nn2_app_sequencer { max_align_t alignment; };
typedef struct {
    nn2_app_seq_config_t cfg;
    nn2_app_seq_state_t state;
    nn2_core_sample_t pending;
    uint32_t next_seq;
    uint32_t cycles_ok;
    uint32_t faults;
    uint32_t off_failures;  /**< failed fail-safe/reset off() calls */
    nn2_status_t last_error;
} nn2_app_seq_internal_t;
_Static_assert(sizeof(nn2_app_seq_internal_t) <= NN2_APP_SEQ_STORAGE_SIZE, "sequencer storage too small");
_Static_assert(_Alignof(nn2_app_seq_storage_t) >= _Alignof(nn2_app_sequencer_t), "sequencer handle alignment");
static inline void nn2_app_seq_load(const nn2_app_sequencer_t *handle, nn2_app_seq_internal_t *state)
{
    const nn2_app_seq_storage_t *storage = (const nn2_app_seq_storage_t *)(const void *)handle;
    memcpy(state, storage->opaque, sizeof(*state));
}
static inline void nn2_app_seq_save(nn2_app_sequencer_t *handle, const nn2_app_seq_internal_t *state)
{
    nn2_app_seq_storage_t *storage = (nn2_app_seq_storage_t *)(void *)handle;
    memcpy(storage->opaque, state, sizeof(*state));
}
#endif
