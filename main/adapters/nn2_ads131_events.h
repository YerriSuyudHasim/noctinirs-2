/* V4-inspired bounded DRDY event record. The caller supplies ISR/task locking.
 * Notifications are wakeups only. This ring is not an ADC conversion FIFO. */
#ifndef NN2_ADS131_EVENTS_H
#define NN2_ADS131_EVENTS_H
#include "nn2_ads131_logic.h"
#define NN2_ADS131_EVENT_CAPACITY 128u
typedef struct {
    nn2_ads131_event_t items[NN2_ADS131_EVENT_CAPACITY];
    uint32_t head, tail, serial, dropped;
    uint64_t latest_us;
    bool enabled;
} nn2_ads131_events_t;
static inline void nn2_ads131_events_clear(nn2_ads131_events_t *q)
{
    q->tail = q->head; q->dropped = 0u;
}
static inline void nn2_ads131_events_push(nn2_ads131_events_t *q, uint64_t time_us)
{
    if (!q->enabled) return;
    q->latest_us = time_us; ++q->serial;
    if (q->head - q->tail == NN2_ADS131_EVENT_CAPACITY) { ++q->dropped; return; }
    q->items[q->head % NN2_ADS131_EVENT_CAPACITY] = (nn2_ads131_event_t){.time_us=time_us, .serial=q->serial, .isr_publish_us=0u};
    ++q->head;
}
static inline uint32_t nn2_ads131_events_take(nn2_ads131_events_t *q,
                                           nn2_ads131_event_t *first, uint32_t *dropped)
{
    uint32_t depth = q->head - q->tail;
    *first = depth ? q->items[q->tail % NN2_ADS131_EVENT_CAPACITY] : (nn2_ads131_event_t){0};
    *dropped = q->dropped;
    uint32_t total = depth + q->dropped;
    /* Pop exactly one event. A depth >2 or any drop is rejected by the
     * owner before reading; retain remaining events for ordered FIFO reads. */
    if (depth) ++q->tail;
    return total;
}
#endif
