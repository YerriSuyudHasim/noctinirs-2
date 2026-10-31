#ifndef NN2_CORE_RAW_BUFFER_H
#define NN2_CORE_RAW_BUFFER_H
#include "nn2_core_optical_codec.h"
#include <stdbool.h>
/* One finalized optical consumer writes, one control task reads. Reset only
 * with both stopped. 128 samples = 16 ms at 8 kSPS; overflow drops newest.
 * Preserve exact per-sample timestamps/epochs; do not estimate them from 125 us. */
#define NN2_RAW_BUFFER_CAPACITY 128u
#define NN2_RAW_BUFFER_MAX_BYTES 6272u
typedef struct {
    uint64_t timestamp;
    int32_t ain[4];
    uint32_t sequence;
    uint16_t epoch;
    uint8_t phase_class, slot_index;
} nn2_raw_entry_t;
typedef struct {
    nn2_raw_entry_t entries[NN2_RAW_BUFFER_CAPACITY];
    /* Separate byte flags avoid padding every 32-byte entry up to 40 bytes.
     * Each byte has the same SPSC ownership as its corresponding entry. */
    bool gap_before[NN2_RAW_BUFFER_CAPACITY];
    uint32_t head, tail, dropped, highwater, packets_sent;
    bool pending_gap, previous_valid;
    uint32_t previous_sequence;
    nn2_oc_raw_t scratch; /* Reader only; never placed on a task stack. */
} nn2_raw_buffer_t;
_Static_assert(sizeof(nn2_raw_entry_t) <= 32u, "RAW entry RAM budget");
_Static_assert(NN2_RAW_BUFFER_CAPACITY >= 128u, "RAW minimum buffering");
_Static_assert(sizeof(nn2_raw_buffer_t) <= NN2_RAW_BUFFER_MAX_BYTES,
    "RAW static RAM budget exceeded");
void nn2_raw_buffer_reset(nn2_raw_buffer_t *);
void nn2_raw_buffer_push(nn2_raw_buffer_t *, const int32_t ain[4],
    uint32_t sequence, uint64_t timestamp, uint16_t epoch,
    uint8_t phase_class, uint8_t slot_index);
size_t nn2_raw_buffer_encode(nn2_raw_buffer_t *, uint8_t *, size_t, uint64_t *);
void nn2_raw_buffer_tx_result(nn2_raw_buffer_t *, bool sent);
#endif
