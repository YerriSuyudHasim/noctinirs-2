#ifndef NN2_FRAME_RING_H
#define NN2_FRAME_RING_H

#include <stddef.h>
#include <stdint.h>

#include "nn2_adc_frame.h"
#include "nn2_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed ring of raw frames. Storage is caller-owned.
   push() never blocks: when full it drops the oldest frame and
   increments dropped_oldest. */
typedef struct {
    nn2_adc_raw_frame_t *storage;
    uint16_t capacity;
    uint16_t head;
    uint16_t count;
    uint32_t dropped_oldest;
} nn2_frame_ring_t;

nn2_status_t nn2_frame_ring_init(nn2_frame_ring_t *ring,
                                 nn2_adc_raw_frame_t *storage,
                                 uint16_t capacity);

/* Stores the new frame. Returns NN2_ERR_OVERFLOW when an older frame
   was dropped. The new frame is still stored. Returns immediately. */
nn2_status_t nn2_frame_ring_push(nn2_frame_ring_t *ring,
                                 const nn2_adc_raw_frame_t *frame);

nn2_status_t nn2_frame_ring_pop(nn2_frame_ring_t *ring,
                                nn2_adc_raw_frame_t *out);

#ifdef __cplusplus
}
#endif

#endif /* NN2_FRAME_RING_H */
