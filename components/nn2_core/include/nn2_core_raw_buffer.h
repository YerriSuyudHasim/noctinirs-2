#ifndef NN2_CORE_RAW_BUFFER_H
#define NN2_CORE_RAW_BUFFER_H
#include "nn2_core_optical_codec.h"
#include <stdbool.h>
/* One finalized optical consumer writes, one control task reads. Reset only
 * with both stopped. 256 samples = 32 ms at 8 kSPS; overflow drops newest. */
#define NN2_RAW_BUFFER_CAPACITY 256u
typedef struct {
    nn2_oc_raw_sample_t sample;
    uint32_t sequence;
    uint64_t timestamp;
    uint16_t epoch;
    bool gap;
} nn2_raw_entry_t;
typedef struct {
    nn2_raw_entry_t entries[NN2_RAW_BUFFER_CAPACITY];
    uint32_t head, tail, dropped, highwater, packets_sent;
    bool pending_gap, previous_valid;
    uint32_t previous_sequence;
    nn2_oc_raw_t scratch; /* Reader only; never placed on a task stack. */
} nn2_raw_buffer_t;
void nn2_raw_buffer_reset(nn2_raw_buffer_t *);
void nn2_raw_buffer_push(nn2_raw_buffer_t *, const int32_t ain[4],
    uint32_t sequence, uint64_t timestamp, uint16_t epoch,
    uint8_t phase_class, uint8_t slot_index);
size_t nn2_raw_buffer_encode(nn2_raw_buffer_t *, uint8_t *, size_t, uint64_t *);
void nn2_raw_buffer_tx_result(nn2_raw_buffer_t *, bool sent);
#endif
