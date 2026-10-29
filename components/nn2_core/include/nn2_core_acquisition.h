/* Single consumer policy; no transport, SDK or shared mutable state. */
#ifndef NN2_CORE_ACQUISITION_H
#define NN2_CORE_ACQUISITION_H
#include "nn2_core_adc_record.h"
#include "nn2_core_protocol.h"
#define NN2_ACQ_K_TARGET 10u
#define NN2_ACQ_K_MIN 8u
#define NN2_ACQ_SETTLE_US 500u
#define NN2_ACQ_FILTER_US 375u /* Full sinc3 support: 3 * 125us, not group delay. */
#define NN2_ACQ_GUARD_US 100u
#define NN2_ACQ_WINDOWS 80u
#define NN2_ACQ_PAYLOAD_SIZE (36u + 20u * 56u)
enum { NN2_ACQ_TARGET, NN2_ACQ_MINIMUM, NN2_ACQ_UNDER_K, NN2_ACQ_INVALID, NN2_ACQ_WARMUP };
typedef struct {
    uint64_t commit_us, end_us, origin_us, slot_start_us;
    uint32_t frame;
    uint8_t phase;
} nn2_core_acq_window_t;
typedef struct {
    int64_t sum[2][4];
    uint32_t first_sequence[2];
    uint16_t commit[2], stable[2];
    uint8_t used[2];
} nn2_core_acq_slot_t;
typedef struct {
    nn2_core_acq_window_t windows[NN2_ACQ_WINDOWS];
    nn2_core_acq_slot_t slots[20];
    uint64_t origin_us;
    uint32_t frame, adc_session, last_sequence, window_count;
    bool active, seen_sample;
} nn2_core_acquisition_t;
bool nn2_core_acquisition_fs_valid(uint8_t fs);
void nn2_core_acquisition_reset(nn2_core_acquisition_t *);
nn2_status_t nn2_core_acquisition_window(nn2_core_acquisition_t *, const nn2_core_acq_window_t *);
/* OK: completed old frame copied to payload, EMPTY: no frame; IO: discontinuity.
 * Caller supplies a full payload buffer. Raw phase means remain ADC counts.
 * A frame is finalized only as chronological raw data enters the next frame.
 * STOP discards the unfinished frame; it must never be paired across restart. */
nn2_status_t nn2_core_acquisition_sample(nn2_core_acquisition_t *, const nn2_core_adc_record_t *, uint8_t *payload);
#endif
