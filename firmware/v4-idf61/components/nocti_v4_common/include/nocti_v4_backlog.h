#pragma once

#include <stddef.h>
#include <stdint.h>

/* Host-testable DRDY backlog policy. ADS131M04 keeps the latest conversion
 * only: a queued event that already has a successor was overwritten and must
 * not be read back as a distinct sample. The newest event is read when it is
 * still the only pending conversion. A deep or stale queue is a counted fault.
 * This is the RUNNING service rule. Startup qualification stays stricter. */

#define NOCTI_V4_BACKLOG_FAULT_DEPTH 16u
#define NOCTI_V4_BACKLOG_FAULT_AGE_US 1000u

typedef enum {
    NOCTI_V4_BACKLOG_READ = 0,
    NOCTI_V4_BACKLOG_DROP,
    NOCTI_V4_BACKLOG_FAULT
} nocti_v4_backlog_action_t;

typedef struct {
    nocti_v4_backlog_action_t action;
} nocti_v4_backlog_decision_t;

typedef struct {
    uint32_t sequence;
    uint32_t age_us;
} nocti_v4_backlog_event_t;

typedef struct {
    uint32_t reads;
    uint32_t missed;
    uint32_t missed_backlog;
    uint32_t catchup;
    uint32_t sequence_gap;
    uint32_t last_sequence;
    uint8_t faulted;
    uint8_t fault_backlog;
    uint8_t fault_sequence;
} nocti_v4_backlog_result_t;

/* Single-producer/single-consumer index rule used by the DRDY event ring:
 * depth = head - tail, slot = index & (capacity - 1), overflow is counted. */
#define NOCTI_V4_SPSC_CAPACITY 8u

typedef struct {
    uint32_t head;
    uint32_t tail;
    uint32_t overflow;
    uint32_t depth_max;
    uint32_t sequence[NOCTI_V4_SPSC_CAPACITY];
} nocti_v4_spsc_t;

static inline nocti_v4_backlog_decision_t nocti_v4_backlog_decide(
    uint32_t pending_after_pop, uint32_t age_us)
{
    nocti_v4_backlog_decision_t decision;
    if (pending_after_pop >= NOCTI_V4_BACKLOG_FAULT_DEPTH ||
        age_us >= NOCTI_V4_BACKLOG_FAULT_AGE_US) {
        decision.action = NOCTI_V4_BACKLOG_FAULT;
    } else if (pending_after_pop > 0u) {
        decision.action = NOCTI_V4_BACKLOG_DROP;
    } else {
        decision.action = NOCTI_V4_BACKLOG_READ;
    }
    return decision;
}

/* last_sequence 0 is the session baseline, matching the ADS task. */
static inline uint32_t nocti_v4_sequence_gap(uint32_t last_sequence,
                                             uint32_t sequence)
{
    if (last_sequence == 0u || sequence == last_sequence + 1u) {
        return 0u;
    }
    return sequence - last_sequence - 1u;
}

/* Walk oldest-first, the same order as the ADS service loop. */
static inline nocti_v4_backlog_result_t nocti_v4_backlog_service(
    const nocti_v4_backlog_event_t *events, uint32_t count)
{
    nocti_v4_backlog_result_t result = {0};
    uint32_t last = 0u;
    uint32_t index;
    if (events == NULL) {
        return result;
    }
    for (index = 0u; index < count; ++index) {
        const uint32_t pending = count - index - 1u;
        const uint32_t gap = nocti_v4_sequence_gap(last, events[index].sequence);
        const nocti_v4_backlog_action_t action =
            nocti_v4_backlog_decide(pending, events[index].age_us).action;
        if (gap != 0u) {
            result.sequence_gap += gap;
            result.missed += gap;
            result.faulted = 1u;
            result.fault_sequence = 1u;
            result.last_sequence = last;
            return result;
        }
        if (action == NOCTI_V4_BACKLOG_DROP) {
            result.missed += 1u;
            result.missed_backlog += 1u;
            result.catchup += 1u;
            last = events[index].sequence;
            continue;
        }
        if (action == NOCTI_V4_BACKLOG_FAULT) {
            result.missed += 1u + pending;
            result.missed_backlog += 1u + pending;
            result.faulted = 1u;
            result.fault_backlog = 1u;
            result.last_sequence = last;
            return result;
        }
        result.reads += 1u;
        last = events[index].sequence;
    }
    result.last_sequence = last;
    return result;
}

static inline int nocti_v4_spsc_push(nocti_v4_spsc_t *ring, uint32_t sequence)
{
    uint32_t depth;
    if (ring == NULL) {
        return 0;
    }
    depth = ring->head - ring->tail;
    if (depth >= NOCTI_V4_SPSC_CAPACITY) {
        ring->overflow += 1u;
        return 0;
    }
    ring->sequence[ring->head & (NOCTI_V4_SPSC_CAPACITY - 1u)] = sequence;
    ring->head += 1u;
    if (depth + 1u > ring->depth_max) {
        ring->depth_max = depth + 1u;
    }
    return 1;
}

static inline int nocti_v4_spsc_pop(nocti_v4_spsc_t *ring, uint32_t *sequence)
{
    if (ring == NULL || sequence == NULL || ring->head == ring->tail) {
        return 0;
    }
    *sequence = ring->sequence[ring->tail & (NOCTI_V4_SPSC_CAPACITY - 1u)];
    ring->tail += 1u;
    return 1;
}
