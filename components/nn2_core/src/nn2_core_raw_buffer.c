#include "nn2_core_raw_buffer.h"
#include <string.h>
#define MASK (NN2_RAW_BUFFER_CAPACITY - 1u)
_Static_assert((NN2_RAW_BUFFER_CAPACITY & MASK) == 0, "power of two");
void nn2_raw_buffer_reset(nn2_raw_buffer_t *b)
{
    /* Both producer and payload reader are stopped; diagnostics remain atomic. */
    b->head = b->tail = 0;
    b->pending_gap = b->previous_valid = false;
    __atomic_store_n(&b->dropped, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&b->highwater, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&b->packets_sent, 0u, __ATOMIC_RELAXED);
}
void nn2_raw_buffer_push(nn2_raw_buffer_t *b, const int32_t ain[4],
    uint32_t sequence, uint64_t timestamp, uint16_t epoch,
    uint8_t phase_class, uint8_t slot_index)
{
    uint32_t head = __atomic_load_n(&b->head, __ATOMIC_RELAXED);
    uint32_t tail = __atomic_load_n(&b->tail, __ATOMIC_ACQUIRE);
    if (head - tail == NN2_RAW_BUFFER_CAPACITY) {
        __atomic_fetch_add(&b->dropped, 1u, __ATOMIC_RELAXED);
        b->pending_gap = true;
        return;
    }
    nn2_raw_entry_t *e = &b->entries[head & MASK];
    memcpy(e->ain, ain, sizeof(e->ain));
    e->phase_class = phase_class;
    e->slot_index = slot_index;
    e->sequence = sequence; e->timestamp = timestamp; e->epoch = epoch;
    b->gap_before[head & MASK] = b->pending_gap;
    b->pending_gap = false;
    uint32_t depth = head - tail + 1u;
    if (depth > __atomic_load_n(&b->highwater, __ATOMIC_RELAXED))
        __atomic_store_n(&b->highwater, depth, __ATOMIC_RELAXED);
    __atomic_store_n(&b->head, head + 1u, __ATOMIC_RELEASE);
}
size_t nn2_raw_buffer_encode(nn2_raw_buffer_t *b, uint8_t *out, size_t cap,
    uint64_t *timestamp)
{
    uint32_t tail = __atomic_load_n(&b->tail, __ATOMIC_RELAXED);
    uint32_t head = __atomic_load_n(&b->head, __ATOMIC_ACQUIRE);
    if (head == tail) return 0;
    const nn2_raw_entry_t *first = &b->entries[tail & MASK];
    nn2_oc_raw_t *raw = &b->scratch;
    raw->slot_plan_epoch = first->epoch;
    raw->first_sample_sequence = first->sequence;
    raw->geometry_map_id = 1;
    raw->raw_flags = 1u | ((b->gap_before[tail & MASK] || (b->previous_valid &&
        first->sequence != b->previous_sequence + 1u)) ? 2u : 0u);
    raw->sample_count = 0;
    while (tail + raw->sample_count != head && raw->sample_count < 100u) {
        const nn2_raw_entry_t *e = &b->entries[(tail + raw->sample_count) & MASK];
        if (e->epoch != raw->slot_plan_epoch ||
            e->sequence != raw->first_sample_sequence + raw->sample_count ||
            (raw->sample_count && b->gap_before[(tail + raw->sample_count) & MASK])) break;
        nn2_oc_raw_sample_t *sample = &raw->sample[raw->sample_count++];
        memcpy(sample->ain, e->ain, sizeof(sample->ain));
        sample->phase_class = e->phase_class;
        sample->slot_index = e->slot_index;
    }
    size_t written = 0;
    nn2_oc_stats_t stats = {0};
    if (nn2_oc_encode_raw(raw, out, cap, &written, &stats) != NN2_OC_OK) return 0;
    *timestamp = first->timestamp;
    b->previous_sequence = raw->first_sample_sequence + raw->sample_count - 1u;
    b->previous_valid = true;
    __atomic_store_n(&b->tail, tail + raw->sample_count, __ATOMIC_RELEASE);
    return written;
}
void nn2_raw_buffer_tx_result(nn2_raw_buffer_t *b, bool sent)
{
    if (sent) __atomic_fetch_add(&b->packets_sent, 1u, __ATOMIC_RELAXED);
    /* UDP loss is exposed by sample sequence and existing send_errors. */
}
