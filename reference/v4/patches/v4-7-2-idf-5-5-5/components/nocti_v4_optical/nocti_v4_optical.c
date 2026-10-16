#include "nocti_v4_optical.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "nocti_v4_common.h"

#define OPTICAL_HISTORY_FRAMES NOCTI_V4_SCHEDULER_HISTORY_FRAMES
#define OPTICAL_TASK_PRIORITY  9u
#define OPTICAL_TASK_CORE      0
#define OPTICAL_TASK_STACK     6144u
#define FRAME_TASK_PRIORITY    5u
#define FRAME_TASK_CORE        0
#define FRAME_TASK_STACK       4096u
#define OPTICAL_TASK_STACK_WORDS (OPTICAL_TASK_STACK / sizeof(StackType_t))
#define FRAME_TASK_STACK_WORDS   (FRAME_TASK_STACK / sizeof(StackType_t))
#define FRAME_RING_MASK (NOCTI_V4_OPTICAL_FRAME_RING_CAPACITY - 1u)
#define RAW_STAGE_CAPACITY 128u
#define RAW_STAGE_MASK (RAW_STAGE_CAPACITY - 1u)
#define FINALIZED_TIMING_CAPACITY NOCTI_V4_SCHEDULER_FINALIZED_RING_CAPACITY
#define FINALIZED_TIMING_MASK (FINALIZED_TIMING_CAPACITY - 1u)
#define OPTICAL_BATCH_SAMPLES 32u
#define PHASE_SWEEP_COUNT 8u

static const uint16_t s_phase_sweep_off_us[PHASE_SWEEP_COUNT] = {
    2150u, 2200u, 2250u, 2300u,
    2350u, 2400u, 2450u, 2500u,
};

_Static_assert((NOCTI_V4_OPTICAL_FRAME_RING_CAPACITY &
                (NOCTI_V4_OPTICAL_FRAME_RING_CAPACITY - 1u)) == 0u,
               "completed frame ring must be a power of two");
_Static_assert((OPTICAL_TASK_STACK % sizeof(StackType_t)) == 0u,
               "optical stack byte count must align to StackType_t");
_Static_assert((FRAME_TASK_STACK % sizeof(StackType_t)) == 0u,
               "frame stack byte count must align to StackType_t");
_Static_assert(NOCTI_V4_ADS_CHANNEL_COUNT == 4u,
               "V4-6 frame layout is explicitly four ADS channels");
_Static_assert((RAW_STAGE_CAPACITY & (RAW_STAGE_CAPACITY - 1u)) == 0u,
               "raw staging capacity must be a power of two");
_Static_assert((FINALIZED_TIMING_CAPACITY & (FINALIZED_TIMING_CAPACITY - 1u)) == 0u,
               "finalized timing capacity must be a power of two");

/* V3 geometry mapping is the audited baseline: each physical geometry has a
 * 735-nm slot then an 850-nm slot. The PCA mapping is resolved from the
 * hardware-verified V4 LED table rather than redefined here. */
static const nocti_v4_optical_slot_descriptor_t s_descriptor[NOCTI_V4_SLOT_COUNT] = {
    { 0u, 0u, 0u,  0u, 735u, 4u, 10u, 10u, 8u, 8u },
    { 1u, 1u, 0u,  1u, 850u, 4u, 10u, 10u, 8u, 8u },
    { 2u, 2u, 1u,  2u, 735u, 4u, 10u, 10u, 8u, 8u },
    { 3u, 3u, 1u,  3u, 850u, 4u, 10u, 10u, 8u, 8u },
    { 4u, 4u, 2u,  4u, 735u, 4u, 10u, 10u, 8u, 8u },
    { 5u, 5u, 2u,  5u, 850u, 4u, 10u, 10u, 8u, 8u },
    { 6u, 6u, 3u,  6u, 735u, 4u, 10u, 10u, 8u, 8u },
    { 7u, 7u, 3u,  7u, 850u, 4u, 10u, 10u, 8u, 8u },
    { 8u, 8u, 4u,  8u, 735u, 4u, 10u, 10u, 8u, 8u },
    { 9u, 9u, 4u,  9u, 850u, 4u, 10u, 10u, 8u, 8u },
    {10u,10u, 5u, 14u, 735u, 4u, 10u, 10u, 8u, 8u },
    {11u,11u, 5u, 15u, 850u, 4u, 10u, 10u, 8u, 8u },
    {12u,12u, 6u, 16u, 735u, 4u, 10u, 10u, 8u, 8u },
    {13u,13u, 6u, 17u, 850u, 4u, 10u, 10u, 8u, 8u },
    {14u,14u, 7u, 18u, 735u, 4u, 10u, 10u, 8u, 8u },
    {15u,15u, 7u, 19u, 850u, 4u, 10u, 10u, 8u, 8u },
    {16u,16u, 8u, 20u, 735u, 4u, 10u, 10u, 8u, 8u },
    {17u,17u, 8u, 21u, 850u, 4u, 10u, 10u, 8u, 8u },
    {18u,18u, 9u, 22u, 735u, 4u, 10u, 10u, 8u, 8u },
    {19u,19u, 9u, 23u, 850u, 4u, 10u, 10u, 8u, 8u },
};

typedef struct {
    bool valid;
    bool finalization_due;
    bool emitted;
    nocti_v4_slot_timing_t timing;
    uint32_t n_on_available;
    uint32_t n_off_available;
    uint32_t v5_candidate_on_equivalent;
    uint32_t v5_candidate_off_equivalent;
    uint8_t phase_candidate_on[PHASE_SWEEP_COUNT];
    uint8_t phase_candidate_off[PHASE_SWEEP_COUNT];
    uint8_t on_tail_count;
    uint8_t off_tail_count;
    uint8_t on_tail_next;
    uint8_t off_tail_next;
    int32_t on_tail[NOCTI_V4_K_ON_TARGET][NOCTI_V4_ADS_CHANNEL_COUNT];
    int32_t off_tail[NOCTI_V4_K_OFF_TARGET][NOCTI_V4_ADS_CHANNEL_COUNT];
} slot_work_t;

typedef enum {
    SAMPLE_CLASS_PRECOMMIT = 0,
    SAMPLE_CLASS_ON_SETTLE,
    SAMPLE_CLASS_ON_STABLE,
    SAMPLE_CLASS_OFF_TRANSITION,
    SAMPLE_CLASS_OFF_SETTLE,
    SAMPLE_CLASS_OFF_STABLE,
    SAMPLE_CLASS_END_GUARD,
    SAMPLE_CLASS_TIMING_GAP,
    SAMPLE_CLASS_INTERNAL_ERROR,
} sample_class_t;

typedef struct {
    bool active;
    bool aborted_by_stop;
    nocti_v4_completed_optical_frame_t frame;
    uint32_t present_mask;
} frame_builder_t;

typedef struct {
    uint32_t slots;
    uint32_t on_0_to_7;
    uint32_t on_8_to_9;
    uint32_t on_ge10;
    uint32_t on_sum;
    uint32_t off_0_to_7;
    uint32_t off_8_to_9;
    uint32_t off_ge10;
    uint32_t off_sum;
    uint32_t frames_target;
    uint32_t frames_partial;
    uint32_t frames_invalid;
} phase_model_stats_t;

typedef struct {
    bool active;
    uint32_t session_epoch;
    uint32_t frame_sequence;
    uint32_t present_mask;
    bool partial;
    bool invalid;
} phase_frame_builder_t;

typedef struct {
    nocti_v4_optical_t *public_optical;
    nocti_v4_ads_t *ads;
    nocti_v4_scheduler_t *scheduler;
    nocti_v4_time_t *timebase;
    nocti_v4_health_t *health;
    TaskHandle_t optical_task;
    TaskHandle_t frame_task;
    StaticTask_t optical_task_storage;
    StaticTask_t frame_task_storage;
    /* ESP-IDF specifies static task stack depth in bytes.  StackType_t is
     * uint8_t on the ESP32-S3 IDF 5.5.5 port; retain an explicit byte-to-item
     * conversion so the backing-store contract is unambiguous. */
    StackType_t optical_stack[OPTICAL_TASK_STACK_WORDS];
    StackType_t frame_stack[FRAME_TASK_STACK_WORDS];
    SemaphoreHandle_t stop_done;
    StaticSemaphore_t stop_done_storage;
    portMUX_TYPE state_lock;
    volatile bool session_prepared;
    volatile bool running;
    volatile bool stopping;
    volatile bool finish_requested;
    uint32_t scheduler_session_epoch;
    uint32_t ads_session_epoch;
    uint64_t first_frame_epoch_us;
    uint64_t last_processed_raw_ts;
    uint64_t last_staged_raw_ts;
    nocti_v4_raw_sample_t raw_stage[RAW_STAGE_CAPACITY];
    uint32_t raw_stage_head;
    uint32_t raw_stage_tail;
    uint32_t raw_stage_depth_max;
    bool staging_integrity_fault;
    bool timing_backlog_integrity_fault;
    nocti_v4_slot_timing_t finalized_timing[FINALIZED_TIMING_CAPACITY];
    uint32_t finalized_timing_head;
    uint32_t finalized_timing_tail;
    slot_work_t work[OPTICAL_HISTORY_FRAMES][NOCTI_V4_SLOT_COUNT];
    frame_builder_t builder;
    volatile uint32_t frame_head;
    volatile uint32_t frame_tail;
    nocti_v4_completed_optical_frame_t frame_ring[NOCTI_V4_OPTICAL_FRAME_RING_CAPACITY];
    nocti_v4_completed_optical_frame_t latest_frame;
    volatile bool latest_frame_valid;
    nocti_v4_k_anomaly_t anomaly[NOCTI_V4_K_ANOMALY_RING_CAPACITY];
    uint32_t anomaly_count;
    uint32_t anomaly_next;
    nocti_v4_classify_mismatch_t mismatch[NOCTI_V4_CLASSIFY_MISMATCH_RING_CAPACITY];
    uint32_t mismatch_count;
    uint32_t mismatch_next;
    phase_model_stats_t phase_stats[PHASE_SWEEP_COUNT];
    phase_frame_builder_t phase_builder[PHASE_SWEEP_COUNT];
    uint32_t phase_model_source_off_us;
    uint64_t optical_batch_duration_sum_us;
    uint32_t optical_batch_duration_count;
    uint64_t optical_batch_drain_duration_sum_us;
    uint64_t optical_batch_classify_duration_sum_us;
    uint32_t optical_batch_phase_duration_count;
    uint64_t optical_frame_emit_duration_sum_us;
    uint32_t optical_frame_emit_duration_count;
    volatile bool frame_consumer_busy;
    nocti_v4_completed_frame_sink_t completed_frame_sink;
    void *completed_frame_sink_context;
    int32_t optical_task_core;
    int32_t frame_task_core;
} optical_runtime_t;

static optical_runtime_t s_runtime;
static bool s_runtime_claimed;

static inline void add(uint32_t *value, uint32_t amount)
{
    (void)__atomic_fetch_add(value, amount, __ATOMIC_RELAXED);
}

static inline void store(uint32_t *value, uint32_t amount)
{
    __atomic_store_n(value, amount, __ATOMIC_RELEASE);
}

static inline uint32_t load(const uint32_t *value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static void min_nonzero(uint32_t *value, uint32_t candidate)
{
    uint32_t old = __atomic_load_n(value, __ATOMIC_RELAXED);
    while ((old == 0u || candidate < old) &&
           !__atomic_compare_exchange_n(value, &old, candidate, false,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

static void update_max(uint32_t *value, uint32_t candidate);

static uint32_t frame_depth(const optical_runtime_t *r)
{
    return __atomic_load_n(&r->frame_head, __ATOMIC_ACQUIRE) -
           __atomic_load_n(&r->frame_tail, __ATOMIC_ACQUIRE);
}

static bool frame_push(optical_runtime_t *r,
                       const nocti_v4_completed_optical_frame_t *frame)
{
    const uint32_t head = __atomic_load_n(&r->frame_head, __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&r->frame_tail, __ATOMIC_ACQUIRE);
    if (head - tail >= NOCTI_V4_OPTICAL_FRAME_RING_CAPACITY) {
        add(&r->health->completed_frame_ring_overflow, 1u);
        return false;
    }
    r->frame_ring[head & FRAME_RING_MASK] = *frame;
    __atomic_store_n(&r->frame_head, head + 1u, __ATOMIC_RELEASE);
    update_max(&r->health->completed_frame_ring_depth_max,
               head + 1u - tail);
    xTaskNotifyGive(r->frame_task);
    return true;
}

static bool frame_pop(optical_runtime_t *r,
                      nocti_v4_completed_optical_frame_t *out_frame)
{
    const uint32_t tail = __atomic_load_n(&r->frame_tail, __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&r->frame_head, __ATOMIC_ACQUIRE);
    if (tail == head) return false;
    *out_frame = r->frame_ring[tail & FRAME_RING_MASK];
    __atomic_store_n(&r->frame_tail, tail + 1u, __ATOMIC_RELEASE);
    return true;
}

static const nocti_v4_optical_slot_descriptor_t *descriptor_for(uint8_t slot_index)
{
    return slot_index < NOCTI_V4_SLOT_COUNT ? &s_descriptor[slot_index] : NULL;
}

static slot_work_t *work_for(optical_runtime_t *r,
                             const nocti_v4_slot_timing_t *timing)
{
    if (!timing->valid || timing->slot_index >= NOCTI_V4_SLOT_COUNT) return NULL;
    slot_work_t *work = &r->work[timing->frame_sequence % OPTICAL_HISTORY_FRAMES]
                              [timing->slot_index];
    if (!work->valid || work->timing.session_epoch != timing->session_epoch ||
        work->timing.frame_sequence != timing->frame_sequence ||
        work->timing.slot_epoch != timing->slot_epoch) {
        /* A history entry must only be recycled after its prior slot was
         * emitted; otherwise the static timeline could silently lose data. */
        if (work->valid && !work->emitted) {
            add(&r->health->representative_compute_errors, 1u);
        }
        memset(work, 0, sizeof(*work));
        work->valid = true;
    }
    work->timing = *timing;
    return work;
}

static void tail_add(int32_t tail[NOCTI_V4_K_ON_TARGET][NOCTI_V4_ADS_CHANNEL_COUNT],
                     uint8_t *count, uint8_t *next,
                     const nocti_v4_raw_sample_t *sample)
{
    memcpy(tail[*next], sample->channel, sizeof(sample->channel));
    *next = (uint8_t)((*next + 1u) % NOCTI_V4_K_ON_TARGET);
    if (*count < NOCTI_V4_K_ON_TARGET) ++*count;
}

/* Actual V2 behavior: newest-to-oldest contiguous integrity-valid tail,
 * average up to fixed target K in int64. ADS only publishes integrity-valid
 * samples, so this bounded chronological tail is exactly that newest tail. */
static uint8_t latest_contiguous_mean(
    const int32_t tail[NOCTI_V4_K_ON_TARGET][NOCTI_V4_ADS_CHANNEL_COUNT],
    uint8_t count, uint8_t next, int32_t out[NOCTI_V4_ADS_CHANNEL_COUNT])
{
    if (count == 0u) {
        memset(out, 0, sizeof(int32_t) * NOCTI_V4_ADS_CHANNEL_COUNT);
        return 0u;
    }
    int64_t sum[NOCTI_V4_ADS_CHANNEL_COUNT] = {0};
    /* `next` is the replacement index, hence the oldest value when full. */
    const uint8_t first = count == NOCTI_V4_K_ON_TARGET ? next : 0u;
    for (uint8_t sample = 0u; sample < count; ++sample) {
        const uint8_t index = (uint8_t)((first + sample) % NOCTI_V4_K_ON_TARGET);
        for (uint8_t channel = 0u; channel < NOCTI_V4_ADS_CHANNEL_COUNT; ++channel) {
            sum[channel] += tail[index][channel];
        }
    }
    for (uint8_t channel = 0u; channel < NOCTI_V4_ADS_CHANNEL_COUNT; ++channel) {
        out[channel] = (int32_t)(sum[channel] / (int64_t)count);
    }
    return count;
}

static void record_anomaly(optical_runtime_t *r,
                           const nocti_v4_slot_timing_t *timing,
                           const nocti_v4_optical_slot_value_t *value)
{
    if (value->n_on_available >= NOCTI_V4_K_ON_TARGET &&
        value->n_off_available >= NOCTI_V4_K_OFF_TARGET) return;
    if (timing == NULL || timing->session_epoch == 0u ||
        timing->frame_sequence == 0u || timing->slot_epoch == 0u ||
        timing->slot_index >= NOCTI_V4_SLOT_COUNT ||
        timing->slot_index != value->slot_index) {
        add(&r->health->anomaly_identity_invalid, 1u);
        return;
    }
    nocti_v4_k_anomaly_t *a = &r->anomaly[r->anomaly_next % NOCTI_V4_K_ANOMALY_RING_CAPACITY];
    *a = (nocti_v4_k_anomaly_t){
        .session_epoch = timing->session_epoch,
        .frame_sequence = timing->frame_sequence,
        .slot_epoch = timing->slot_epoch,
        .slot_index = value->slot_index,
        .n_on = value->n_on_available,
        .n_off = value->n_off_available,
        .scheduler_lateness_us = value->scheduler_lateness_us,
        .led_request_to_submit_us = timing->led_on_submit_ts >=
                                    timing->led_on_request_ts ?
            (uint32_t)(timing->led_on_submit_ts - timing->led_on_request_ts) : 0u,
        .led_on_latency_us = value->led_on_latency_us,
        .led_off_latency_us = value->led_off_latency_us,
    };
    ++r->anomaly_next;
    if (r->anomaly_count < NOCTI_V4_K_ANOMALY_RING_CAPACITY) ++r->anomaly_count;
}

static void builder_start(frame_builder_t *builder, const nocti_v4_slot_timing_t *timing)
{
    memset(builder, 0, sizeof(*builder));
    builder->active = true;
    builder->frame.session_epoch = timing->session_epoch;
    builder->frame.frame_sequence = timing->frame_sequence;
    builder->frame.nominal_frame_timestamp_us = timing->nominal_slot_start_ts -
        (uint64_t)timing->slot_index * NOCTI_V4_SLOT_PERIOD_US;
    for (uint8_t geometry = 0u; geometry < NOCTI_V4_SLOT_COUNT / 2u; ++geometry) {
        builder->frame.pair[geometry].geometry_id = geometry;
        builder->frame.pair[geometry].slot_735 = (uint8_t)(geometry * 2u);
        builder->frame.pair[geometry].slot_850 = (uint8_t)(geometry * 2u + 1u);
    }
}

static bool is_startup_warmup_frame(const nocti_v4_slot_timing_t *timing)
{
    return timing != NULL && timing->frame_sequence >= 1u &&
           timing->frame_sequence <= NOCTI_V4_SCAN_WARMUP_FRAMES;
}

static void finalize_builder(optical_runtime_t *r, bool force_abort,
                             uint64_t completed_timestamp_us)
{
    frame_builder_t *b = &r->builder;
    if (!b->active) return;
    nocti_v4_completed_optical_frame_t *frame = &b->frame;
    frame->completed_timestamp_us = completed_timestamp_us;
    const uint32_t all_slots = (1u << NOCTI_V4_SLOT_COUNT) - 1u;
    const uint32_t missing = all_slots & ~b->present_mask;
    const bool startup_warmup = frame->frame_sequence >= 1u &&
        frame->frame_sequence <= NOCTI_V4_SCAN_WARMUP_FRAMES;
    if (force_abort || b->aborted_by_stop) {
        frame->quality = NOCTI_V4_FRAME_QUALITY_ABORTED_BY_STOP;
        frame->reason |= NOCTI_V4_SLOT_REASON_STOP_ABORT;
        add(&r->health->frames_aborted_by_stop, 1u);
    } else if (startup_warmup) {
        /* Frame 1 exercises the complete physical pipeline but is explicitly
         * excluded from scientific output and network ownership. Under-K at
         * this alignment boundary is expected; structural/integrity reasons
         * remain visible as a warm-up failure. */
        if (missing != 0u) {
            frame->reason |= NOCTI_V4_SLOT_REASON_INTERNAL;
        }
        frame->quality = NOCTI_V4_FRAME_QUALITY_STARTUP_WARMUP;
        frame->reason |= NOCTI_V4_SLOT_REASON_STARTUP_WARMUP;
        add(&r->health->frames_startup_warmup, 1u);
        const uint32_t structural_reasons = frame->reason &
            ~(NOCTI_V4_SLOT_REASON_UNDER_K_ON |
              NOCTI_V4_SLOT_REASON_UNDER_K_OFF |
              NOCTI_V4_SLOT_REASON_STARTUP_WARMUP);
        if (missing != 0u || structural_reasons != 0u) {
            add(&r->health->frames_startup_warmup_failed, 1u);
        }
    } else {
        if (missing != 0u) {
            frame->invalid_slots += (uint8_t)__builtin_popcount(missing);
            frame->reason |= NOCTI_V4_SLOT_REASON_INTERNAL;
        }
        if (frame->invalid_slots != 0u) {
            frame->quality = NOCTI_V4_FRAME_QUALITY_INVALID;
            add(&r->health->frames_invalid_runtime, 1u);
        } else if (frame->minimum_valid_slots != 0u) {
            frame->quality = NOCTI_V4_FRAME_QUALITY_PARTIAL;
            add(&r->health->frames_partial, 1u);
        } else {
            frame->quality = NOCTI_V4_FRAME_QUALITY_TARGET_VALID;
            add(&r->health->frames_target_valid, 1u);
        }
        add(&r->health->frames_total, 1u);
        uint64_t emit_start_us = 0u;
        uint64_t emit_end_us = 0u;
        const bool emit_timed = nocti_v4_time_now_us(r->timebase,
                                                      &emit_start_us) == ESP_OK;
        (void)frame_push(r, frame);
        if (emit_timed && nocti_v4_time_now_us(r->timebase, &emit_end_us) == ESP_OK &&
            emit_end_us >= emit_start_us) {
            const uint32_t duration = (uint32_t)(emit_end_us - emit_start_us);
            r->optical_frame_emit_duration_sum_us += duration;
            ++r->optical_frame_emit_duration_count;
            update_max(&r->health->optical_frame_emit_duration_max_us, duration);
            store(&r->health->optical_frame_emit_duration_avg_us,
                  (uint32_t)(r->optical_frame_emit_duration_sum_us /
                             r->optical_frame_emit_duration_count));
        }
    }
    memset(b, 0, sizeof(*b));
}

static void builder_add_slot(optical_runtime_t *r, const nocti_v4_slot_timing_t *timing,
                             const nocti_v4_optical_slot_value_t *value)
{
    frame_builder_t *b = &r->builder;
    if (!b->active || b->frame.session_epoch != timing->session_epoch ||
        b->frame.frame_sequence != timing->frame_sequence) {
        finalize_builder(r, false, timing->actual_finalize_ts);
        builder_start(b, timing);
    }
    const uint32_t bit = 1u << value->slot_index;
    if ((b->present_mask & bit) != 0u) {
        b->frame.invalid_slots++;
        b->frame.reason |= NOCTI_V4_SLOT_REASON_INTERNAL;
        return;
    }
    b->frame.slot[value->slot_index] = *value;
    b->present_mask |= bit;
    if (value->quality == NOCTI_V4_SLOT_QUALITY_STARTUP_WARMUP) {
        /* Warm-up slots are neither target-valid nor scientific failures. */
    } else if (value->quality == NOCTI_V4_SLOT_QUALITY_TARGET_VALID) ++b->frame.target_valid_slots;
    else if (value->quality == NOCTI_V4_SLOT_QUALITY_MINIMUM_VALID) ++b->frame.minimum_valid_slots;
    else ++b->frame.invalid_slots;
    b->frame.reason |= value->reason;
    nocti_v4_wavelength_pair_t *pair = &b->frame.pair[value->geometry_id];
    if (value->wavelength_nm == 735u) {
        pair->present_735 = true;
        pair->valid_735 = value->quality != NOCTI_V4_SLOT_QUALITY_INVALID_UNDER_K;
        memcpy(pair->i735, value->i_corrected, sizeof(pair->i735));
    } else {
        pair->present_850 = true;
        pair->valid_850 = value->quality != NOCTI_V4_SLOT_QUALITY_INVALID_UNDER_K;
        memcpy(pair->i850, value->i_corrected, sizeof(pair->i850));
    }
    if (value->quality == NOCTI_V4_SLOT_QUALITY_ABORTED_BY_STOP) b->aborted_by_stop = true;
    if (value->slot_index + 1u == NOCTI_V4_SLOT_COUNT) {
        finalize_builder(r, false, timing->actual_finalize_ts);
    }
}

static void record_k_counters(optical_runtime_t *r, const nocti_v4_optical_slot_value_t *value)
{
    const uint32_t on = value->n_on_available;
    const uint32_t off = value->n_off_available;
    min_nonzero(&r->health->candidate_min_on, on);
    min_nonzero(&r->health->candidate_min_off, off);
    if (on < NOCTI_V4_K_ON_MINIMUM) {
        add(&r->health->candidate_under8_on, 1u); add(&r->health->candidate_on_0_to_7, 1u);
    } else if (on < NOCTI_V4_K_ON_TARGET) {
        add(&r->health->candidate_under10_on, 1u); add(&r->health->candidate_on_8_to_9, 1u);
    } else if (on <= 12u) add(&r->health->candidate_on_10_to_12, 1u);
    else add(&r->health->candidate_on_gt_12, 1u);
    if (off < NOCTI_V4_K_OFF_MINIMUM) {
        add(&r->health->candidate_under8_off, 1u); add(&r->health->candidate_off_0_to_7, 1u);
    } else if (off < NOCTI_V4_K_OFF_TARGET) {
        add(&r->health->candidate_under10_off, 1u); add(&r->health->candidate_off_8_to_9, 1u);
    } else if (off <= 12u) add(&r->health->candidate_off_10_to_12, 1u);
    else add(&r->health->candidate_off_gt_12, 1u);
}

static void record_phase_model_slot(optical_runtime_t *r,
                                    const slot_work_t *work,
                                    bool structural_invalid)
{
    const nocti_v4_slot_timing_t *timing = &work->timing;
    const uint32_t complete_mask = (1u << NOCTI_V4_SLOT_COUNT) - 1u;
    for (uint8_t index = 0u; index < PHASE_SWEEP_COUNT; ++index) {
        phase_model_stats_t *stats = &r->phase_stats[index];
        phase_frame_builder_t *builder = &r->phase_builder[index];
        const uint32_t on = work->phase_candidate_on[index];
        const uint32_t off = work->phase_candidate_off[index];
        add(&stats->slots, 1u);
        add(&stats->on_sum, on);
        add(&stats->off_sum, off);
        if (on < NOCTI_V4_K_ON_MINIMUM) add(&stats->on_0_to_7, 1u);
        else if (on < NOCTI_V4_K_ON_TARGET) add(&stats->on_8_to_9, 1u);
        else add(&stats->on_ge10, 1u);
        if (off < NOCTI_V4_K_OFF_MINIMUM) add(&stats->off_0_to_7, 1u);
        else if (off < NOCTI_V4_K_OFF_TARGET) add(&stats->off_8_to_9, 1u);
        else add(&stats->off_ge10, 1u);

        if (timing->slot_index == 0u || !builder->active ||
            builder->session_epoch != timing->session_epoch ||
            builder->frame_sequence != timing->frame_sequence) {
            *builder = (phase_frame_builder_t){
                .active = true,
                .session_epoch = timing->session_epoch,
                .frame_sequence = timing->frame_sequence,
            };
        }
        const uint32_t slot_bit = 1u << timing->slot_index;
        if ((builder->present_mask & slot_bit) != 0u) {
            builder->invalid = true;
        }
        builder->present_mask |= slot_bit;
        if (structural_invalid || on < NOCTI_V4_K_ON_MINIMUM ||
            off < NOCTI_V4_K_OFF_MINIMUM) {
            builder->invalid = true;
        } else if (on < NOCTI_V4_K_ON_TARGET ||
                   off < NOCTI_V4_K_OFF_TARGET) {
            builder->partial = true;
        }
        if (timing->slot_index + 1u == NOCTI_V4_SLOT_COUNT) {
            if (builder->present_mask != complete_mask) {
                builder->invalid = true;
            }
            if (builder->invalid) add(&stats->frames_invalid, 1u);
            else if (builder->partial) add(&stats->frames_partial, 1u);
            else add(&stats->frames_target, 1u);
            builder->active = false;
        }
    }
}

static void finalize_work(optical_runtime_t *r, slot_work_t *work)
{
    if (!work->valid || work->emitted || !work->finalization_due) return;
    const nocti_v4_slot_timing_t *t = &work->timing;
    const nocti_v4_optical_slot_descriptor_t *d = descriptor_for(t->slot_index);
    if (d == NULL) { add(&r->health->representative_compute_errors, 1u); return; }
    nocti_v4_optical_slot_value_t value = {
        .slot_index = t->slot_index, .logical_source = t->logical_source,
        .geometry_id = d->geometry_id, .pca_channel = t->pca_channel,
        .wavelength_nm = d->wavelength_nm,
        .n_on_available = (uint8_t)(work->n_on_available > UINT8_MAX ? UINT8_MAX : work->n_on_available),
        .n_off_available = (uint8_t)(work->n_off_available > UINT8_MAX ? UINT8_MAX : work->n_off_available),
        .scheduler_lateness_us = t->scheduler_on_handling_ts >= t->scheduler_on_deadline_ts ?
            (uint32_t)(t->scheduler_on_handling_ts - t->scheduler_on_deadline_ts) : 0u,
        .led_on_latency_us = t->led_on_complete_ts >= t->led_on_submit_ts ?
            (uint32_t)(t->led_on_complete_ts - t->led_on_submit_ts) : 0u,
        .led_off_latency_us = t->led_off_complete_ts >= t->led_off_submit_ts ?
            (uint32_t)(t->led_off_complete_ts - t->led_off_submit_ts) : 0u,
    };
    nocti_v4_ads_status_snapshot_t ads_status;
    nocti_v4_ads_get_status(r->ads, &ads_status);
    if (t->aborted_by_stop || r->stopping) {
        value.quality = NOCTI_V4_SLOT_QUALITY_ABORTED_BY_STOP;
        value.reason = NOCTI_V4_SLOT_REASON_STOP_ABORT;
        add(&r->health->slots_aborted_by_stop, 1u);
    } else {
        if (t->runtime_invalid) value.reason |= NOCTI_V4_SLOT_REASON_RUNTIME_TIMING;
        if (t->led_on_commit_ts == 0u) value.reason |= NOCTI_V4_SLOT_REASON_LED_ON_MISSING;
        if (t->led_off_commit_ts == 0u) value.reason |= NOCTI_V4_SLOT_REASON_LED_OFF_MISSING;
        if (ads_status.session_integrity_fault) value.reason |= NOCTI_V4_SLOT_REASON_ADS_INTEGRITY;
        if (value.n_on_available < d->k_min_on) value.reason |= NOCTI_V4_SLOT_REASON_UNDER_K_ON;
        if (value.n_off_available < d->k_min_off) value.reason |= NOCTI_V4_SLOT_REASON_UNDER_K_OFF;
        value.n_on_used = latest_contiguous_mean(work->on_tail, work->on_tail_count,
                                                 work->on_tail_next, value.i_on);
        value.n_off_used = latest_contiguous_mean(work->off_tail, work->off_tail_count,
                                                  work->off_tail_next, value.i_led_off);
        for (uint8_t channel = 0u; channel < NOCTI_V4_ADS_CHANNEL_COUNT; ++channel) {
            value.i_corrected[channel] = (int64_t)value.i_on[channel] -
                                         (int64_t)value.i_led_off[channel];
        }
        if (is_startup_warmup_frame(t)) {
            value.quality = NOCTI_V4_SLOT_QUALITY_STARTUP_WARMUP;
            value.reason |= NOCTI_V4_SLOT_REASON_STARTUP_WARMUP;
            add(&r->health->slots_startup_warmup, 1u);
        } else if (value.reason != NOCTI_V4_SLOT_REASON_NONE) {
            value.quality = NOCTI_V4_SLOT_QUALITY_INVALID_UNDER_K;
            add(&r->health->slots_invalid_runtime, 1u);
        } else if (value.n_on_available < d->k_target_on || value.n_off_available < d->k_target_off) {
            value.quality = NOCTI_V4_SLOT_QUALITY_MINIMUM_VALID;
            add(&r->health->slots_minimum_valid, 1u);
        } else {
            value.quality = NOCTI_V4_SLOT_QUALITY_TARGET_VALID;
            add(&r->health->slots_target_valid, 1u);
        }
        if (!is_startup_warmup_frame(t)) {
            record_k_counters(r, &value);
            record_anomaly(r, t, &value);
            const bool structural_invalid = t->runtime_invalid ||
                t->led_on_commit_ts == 0u || t->led_off_commit_ts == 0u ||
                ads_status.session_integrity_fault;
            record_phase_model_slot(r, work, structural_invalid);
        }
    }
    builder_add_slot(r, t, &value);
    work->emitted = true;
}

static void count_category(optical_runtime_t *r, uint32_t *counter)
{
    add(&r->health->optical_raw_processed, 1u);
    add(counter, 1u);
}

static bool timing_is_complete(const nocti_v4_slot_timing_t *t)
{
    return t != NULL && t->valid && t->finalized &&
           t->led_on_request_ts != 0u && t->led_on_queue_ts != 0u &&
           t->led_on_dequeue_ts != 0u && t->led_on_submit_ts != 0u &&
           t->led_on_commit_ts != 0u && t->led_on_stable_start_ts != 0u &&
           t->led_off_transition_boundary_ts != 0u && t->led_off_request_ts != 0u &&
           t->led_off_queue_ts != 0u && t->led_off_dequeue_ts != 0u &&
           t->led_off_submit_ts != 0u && t->led_off_commit_ts != 0u &&
           t->led_off_stable_start_ts != 0u && t->slot_guard_start_ts != 0u &&
           t->nominal_slot_end_ts != 0u;
}

/* The sole definition of optical boundaries. V4-5-equivalent candidate
 * counters and V4-6 representatives are both generated from this result;
 * no mutable scheduler snapshot is accepted here. */
static sample_class_t canonical_classify(const nocti_v4_slot_timing_t *t, uint64_t ts)
{
    if (!timing_is_complete(t) || ts < t->nominal_slot_start_ts ||
        ts >= t->nominal_slot_end_ts) {
        return SAMPLE_CLASS_TIMING_GAP;
    }
    if (ts >= t->slot_guard_start_ts) return SAMPLE_CLASS_END_GUARD;
    if (ts < t->led_on_commit_ts) return SAMPLE_CLASS_PRECOMMIT;
    if (ts < t->led_on_stable_start_ts) return SAMPLE_CLASS_ON_SETTLE;
    if (ts < t->led_off_transition_boundary_ts) return SAMPLE_CLASS_ON_STABLE;
    if (ts < t->led_off_commit_ts) return SAMPLE_CLASS_OFF_TRANSITION;
    if (ts < t->led_off_stable_start_ts) return SAMPLE_CLASS_OFF_SETTLE;
    return SAMPLE_CLASS_OFF_STABLE;
}

static uint64_t shifted_timestamp(uint64_t timestamp_us, int32_t delta_us)
{
    if (delta_us >= 0) {
        return timestamp_us + (uint32_t)delta_us;
    }
    const uint32_t magnitude = (uint32_t)(-delta_us);
    return timestamp_us > magnitude ? timestamp_us - magnitude : 0u;
}

static void record_phase_sample(optical_runtime_t *r, slot_work_t *work,
                                const nocti_v4_slot_timing_t *timing,
                                uint64_t drdy_timestamp_us)
{
    const uint64_t physical_phase_u64 = timing->nominal_led_off_ts >=
                                        timing->nominal_slot_start_ts ?
        timing->nominal_led_off_ts - timing->nominal_slot_start_ts : 0u;
    if (physical_phase_u64 > INT32_MAX) {
        return;
    }
    const int32_t physical_phase_us = (int32_t)physical_phase_u64;
    if (r->phase_model_source_off_us == 0u) {
        r->phase_model_source_off_us = (uint32_t)physical_phase_us;
    }
    for (uint8_t index = 0u; index < PHASE_SWEEP_COUNT; ++index) {
        const int32_t delta_us = (int32_t)s_phase_sweep_off_us[index] -
                                 physical_phase_us;
        const uint64_t model_off_transition = shifted_timestamp(
            timing->led_off_transition_boundary_ts, delta_us);
        const uint64_t model_off_stable = shifted_timestamp(
            timing->led_off_stable_start_ts, delta_us);
        if (drdy_timestamp_us >= timing->led_on_stable_start_ts &&
            drdy_timestamp_us < model_off_transition &&
            work->phase_candidate_on[index] != UINT8_MAX) {
            ++work->phase_candidate_on[index];
        }
        if (drdy_timestamp_us >= model_off_stable &&
            drdy_timestamp_us < timing->slot_guard_start_ts &&
            work->phase_candidate_off[index] != UINT8_MAX) {
            ++work->phase_candidate_off[index];
        }
    }
}

static void update_max(uint32_t *value, uint32_t candidate)
{
    uint32_t old = __atomic_load_n(value, __ATOMIC_RELAXED);
    while (candidate > old && !__atomic_compare_exchange_n(
               value, &old, candidate, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

static bool raw_stage_push(optical_runtime_t *r, const nocti_v4_raw_sample_t *sample)
{
    const uint32_t depth = r->raw_stage_head - r->raw_stage_tail;
    if (depth >= RAW_STAGE_CAPACITY) {
        r->staging_integrity_fault = true;
        count_category(r, &r->health->optical_raw_internal_error);
        add(&r->health->optical_staging_overflow, 1u);
        return false;
    }
    r->raw_stage[r->raw_stage_head & RAW_STAGE_MASK] = *sample;
    ++r->raw_stage_head;
    r->last_staged_raw_ts = sample->drdy_timestamp_us;
    if (depth + 1u > r->raw_stage_depth_max) r->raw_stage_depth_max = depth + 1u;
    return true;
}

static bool timing_push(optical_runtime_t *r, const nocti_v4_slot_timing_t *timing)
{
    const uint32_t depth = r->finalized_timing_head - r->finalized_timing_tail;
    if (depth >= FINALIZED_TIMING_CAPACITY) {
        r->timing_backlog_integrity_fault = true;
        add(&r->health->optical_finalized_timing_backlog_overflow, 1u);
        return false;
    }
    r->finalized_timing[r->finalized_timing_head & FINALIZED_TIMING_MASK] = *timing;
    ++r->finalized_timing_head;
    return true;
}

static void pull_finalized_timings(optical_runtime_t *r)
{
    nocti_v4_slot_timing_t timing;
    while (nocti_v4_scheduler_pop_finalized_slot(r->scheduler, &timing)) {
        (void)timing_push(r, &timing);
    }
}

static void classify_complete_sample(optical_runtime_t *r, slot_work_t *work,
                                     const nocti_v4_slot_timing_t *timing,
                                     const nocti_v4_raw_sample_t *sample)
{
    const uint64_t ts = sample->drdy_timestamp_us;
    /* Counterfactual replay is fed by the same actual DRDY records and the
     * same immutable finalized slot timing used by the canonical classifier.
     * Only the measured OFF transition/commit boundaries are translated; the
     * active session GPTimer deadline is never changed by the model. */
    record_phase_sample(r, work, timing, ts);
    switch (canonical_classify(timing, ts)) {
    case SAMPLE_CLASS_PRECOMMIT:
        count_category(r, &r->health->optical_raw_precommit);
        add(&r->health->raw_rejected_precommit, 1u);
        if (ts < timing->led_on_request_ts) {
            add(&r->health->optical_raw_before_on_request, 1u);
        } else if (ts < timing->led_on_submit_ts) {
            add(&r->health->optical_raw_on_request_to_submit, 1u);
        } else if (ts < timing->led_on_commit_ts) {
            add(&r->health->optical_raw_on_submit_to_complete, 1u);
        } else {
            add(&r->health->optical_raw_other_precommit, 1u);
        }
        break;
    case SAMPLE_CLASS_ON_SETTLE:
        count_category(r, &r->health->optical_raw_on_settle);
        add(&r->health->raw_rejected_on_settle, 1u);
        break;
    case SAMPLE_CLASS_ON_STABLE:
        count_category(r, &r->health->optical_raw_on_stable);
        ++work->v5_candidate_on_equivalent;
        ++work->n_on_available;
        tail_add(work->on_tail, &work->on_tail_count, &work->on_tail_next, sample);
        add(&r->health->raw_candidate_on, 1u);
        break;
    case SAMPLE_CLASS_OFF_TRANSITION:
        count_category(r, &r->health->optical_raw_off_transition);
        add(&r->health->raw_rejected_transition, 1u);
        break;
    case SAMPLE_CLASS_OFF_SETTLE:
        count_category(r, &r->health->optical_raw_off_settle);
        add(&r->health->raw_rejected_off_settle, 1u);
        break;
    case SAMPLE_CLASS_OFF_STABLE:
        count_category(r, &r->health->optical_raw_off_stable);
        ++work->v5_candidate_off_equivalent;
        ++work->n_off_available;
        tail_add(work->off_tail, &work->off_tail_count, &work->off_tail_next, sample);
        add(&r->health->raw_candidate_off, 1u);
        break;
    case SAMPLE_CLASS_END_GUARD:
        count_category(r, &r->health->optical_raw_end_guard);
        add(&r->health->raw_rejected_guard, 1u);
        break;
    case SAMPLE_CLASS_TIMING_GAP:
        count_category(r, &r->health->optical_raw_timing_gap);
        break;
    default:
        count_category(r, &r->health->optical_raw_internal_error);
        break;
    }
}

static void record_parity(optical_runtime_t *r, const slot_work_t *work)
{
    const int32_t on_difference = (int32_t)work->v5_candidate_on_equivalent -
                                  (int32_t)work->n_on_available;
    const int32_t off_difference = (int32_t)work->v5_candidate_off_equivalent -
                                   (int32_t)work->n_off_available;
    const uint32_t abs_on = (uint32_t)(on_difference < 0 ? -on_difference : on_difference);
    const uint32_t abs_off = (uint32_t)(off_difference < 0 ? -off_difference : off_difference);
    update_max(&r->health->classification_max_abs_on_difference, abs_on);
    update_max(&r->health->classification_max_abs_off_difference, abs_off);
    if (on_difference == 0 && off_difference == 0) return;
    add(&r->health->classification_count_mismatch, 1u);
    nocti_v4_classify_mismatch_t *m =
        &r->mismatch[r->mismatch_next % NOCTI_V4_CLASSIFY_MISMATCH_RING_CAPACITY];
    *m = (nocti_v4_classify_mismatch_t){
        .session_epoch = work->timing.session_epoch,
        .frame_sequence = work->timing.frame_sequence,
        .slot_index = work->timing.slot_index,
        .v5_candidate_on_equivalent = (uint8_t)work->v5_candidate_on_equivalent,
        .v5_candidate_off_equivalent = (uint8_t)work->v5_candidate_off_equivalent,
        .v6_n_on_available = (uint8_t)work->n_on_available,
        .v6_n_off_available = (uint8_t)work->n_off_available,
        .difference_on = (int8_t)on_difference,
        .difference_off = (int8_t)off_difference,
    };
    ++r->mismatch_next;
    if (r->mismatch_count < NOCTI_V4_CLASSIFY_MISMATCH_RING_CAPACITY) ++r->mismatch_count;
}

static void classify_ready_timings(optical_runtime_t *r, bool force)
{
    while (r->finalized_timing_tail != r->finalized_timing_head) {
        const nocti_v4_slot_timing_t *timing =
            &r->finalized_timing[r->finalized_timing_tail & FINALIZED_TIMING_MASK];
        if (!force && (r->raw_stage_head == r->raw_stage_tail ||
                       r->last_staged_raw_ts < timing->nominal_slot_end_ts)) {
            return;
        }
        slot_work_t *work = work_for(r, timing);
        if (work == NULL || timing->session_epoch != r->scheduler_session_epoch) {
            r->staging_integrity_fault = true;
        } else {
            while (r->raw_stage_tail != r->raw_stage_head) {
                const nocti_v4_raw_sample_t *sample =
                    &r->raw_stage[r->raw_stage_tail & RAW_STAGE_MASK];
                if (sample->drdy_timestamp_us >= timing->nominal_slot_end_ts) break;
                ++r->raw_stage_tail;
                r->last_processed_raw_ts = sample->drdy_timestamp_us;
                /* Overlay accounting only: canonical mutually-exclusive
                 * categories below still reconcile every raw sample exactly. */
                if (is_startup_warmup_frame(timing)) {
                    add(&r->health->raw_samples_startup_warmup, 1u);
                }
                /* ADS and scheduler carry independently owned session epochs.
                 * The sample must match the captured ADS epoch; the immutable
                 * timing record was separately checked against the scheduler
                 * epoch when it was accepted above. */
                if (sample->session_epoch != r->ads_session_epoch) {
                    count_category(r, &r->health->optical_raw_stale_epoch);
                } else if (sample->drdy_timestamp_us < timing->nominal_slot_start_ts) {
                    count_category(r, &r->health->optical_raw_timing_gap);
                } else if (timing->aborted_by_stop || r->stopping) {
                    count_category(r, &r->health->optical_raw_stop_boundary);
                } else if (!timing_is_complete(timing)) {
                    count_category(r, &r->health->optical_raw_internal_error);
                    r->staging_integrity_fault = true;
                } else {
                    classify_complete_sample(r, work, timing, sample);
                }
            }
            work->finalization_due = true;
            if (r->staging_integrity_fault || r->timing_backlog_integrity_fault) {
                work->timing.runtime_invalid = true;
            }
            record_parity(r, work);
            finalize_work(r, work);
        }
        ++r->finalized_timing_tail;
    }
}

static void stage_raw(optical_runtime_t *r, const nocti_v4_raw_sample_t *sample)
{
    if (!r->running) {
        count_category(r, r->stopping ? &r->health->optical_raw_stop_boundary :
                       r->session_prepared ? &r->health->optical_raw_startup_discard :
                                             &r->health->optical_raw_session_boundary);
    } else if (sample->session_epoch != r->ads_session_epoch) {
        count_category(r, &r->health->optical_raw_stale_epoch);
    } else if (sample->drdy_timestamp_us < r->first_frame_epoch_us) {
        count_category(r, &r->health->optical_raw_startup_discard);
    } else {
        (void)raw_stage_push(r, sample);
    }
}

static void abort_pending(optical_runtime_t *r)
{
    pull_finalized_timings(r);
    classify_ready_timings(r, true);
    while (r->raw_stage_tail != r->raw_stage_head) {
        ++r->raw_stage_tail;
        count_category(r, &r->health->optical_raw_stop_boundary);
    }
    for (uint8_t frame = 0u; frame < OPTICAL_HISTORY_FRAMES; ++frame) {
        for (uint8_t slot = 0u; slot < NOCTI_V4_SLOT_COUNT; ++slot) {
            slot_work_t *work = &r->work[frame][slot];
            if (work->valid && !work->emitted) {
                work->timing.aborted_by_stop = true;
                work->finalization_due = true;
                finalize_work(r, work);
            }
        }
    }
    finalize_builder(r, true, r->last_processed_raw_ts);
    const nocti_v4_health_t *h = r->health;
    const uint32_t categorized = h->optical_raw_precommit + h->optical_raw_on_settle +
        h->optical_raw_on_stable + h->optical_raw_off_transition +
        h->optical_raw_off_settle + h->optical_raw_off_stable +
        h->optical_raw_end_guard + h->optical_raw_startup_discard +
        h->optical_raw_session_boundary + h->optical_raw_stop_boundary +
        h->optical_raw_stale_epoch + h->optical_raw_recovery +
        h->optical_raw_other_explicit + h->optical_raw_timing_gap +
        h->optical_raw_internal_error;
    if (categorized != h->optical_raw_processed) {
        add(&r->health->optical_classification_accounting_mismatch, 1u);
    }
}

static uint32_t process_optical_batch(optical_runtime_t *r)
{
    uint64_t start_us = 0u;
    uint64_t drain_end_us = 0u;
    uint64_t end_us = 0u;
    const bool timing_valid = nocti_v4_time_now_us(r->timebase, &start_us) == ESP_OK;
    uint32_t samples = 0u;
    nocti_v4_raw_sample_t sample;
    while (samples < OPTICAL_BATCH_SAMPLES &&
           nocti_v4_ads_pop_raw_for_bound_consumer(r->ads, &sample)) {
        stage_raw(r, &sample);
        ++samples;
    }
    const bool drain_timed = timing_valid &&
        nocti_v4_time_now_us(r->timebase, &drain_end_us) == ESP_OK;
    /* Pull/classify once per bounded batch rather than once per 8-kSPS
     * sample. This preserves ring order and finalized-timing semantics while
     * reducing core-0 cache/dispatcher pressure. */
    pull_finalized_timings(r);
    classify_ready_timings(r, false);
    if (samples != 0u) {
        add(&r->health->optical_batches, 1u);
        update_max(&r->health->optical_batch_samples_max, samples);
        if (timing_valid &&
            nocti_v4_time_now_us(r->timebase, &end_us) == ESP_OK &&
            end_us >= start_us) {
            const uint64_t duration64 = end_us - start_us;
            const uint32_t duration = duration64 > UINT32_MAX ?
                                      UINT32_MAX : (uint32_t)duration64;
            r->optical_batch_duration_sum_us += duration;
            ++r->optical_batch_duration_count;
            update_max(&r->health->optical_batch_duration_max_us, duration);
            store(&r->health->optical_batch_duration_avg_us,
                  (uint32_t)(r->optical_batch_duration_sum_us /
                             r->optical_batch_duration_count));
            if (drain_timed && drain_end_us >= start_us && end_us >= drain_end_us) {
                const uint32_t drain_duration = (uint32_t)(drain_end_us - start_us);
                const uint32_t classify_duration = (uint32_t)(end_us - drain_end_us);
                r->optical_batch_drain_duration_sum_us += drain_duration;
                r->optical_batch_classify_duration_sum_us += classify_duration;
                ++r->optical_batch_phase_duration_count;
                update_max(&r->health->optical_batch_drain_duration_max_us,
                           drain_duration);
                update_max(&r->health->optical_batch_classify_duration_max_us,
                           classify_duration);
                store(&r->health->optical_batch_drain_duration_avg_us,
                      (uint32_t)(r->optical_batch_drain_duration_sum_us /
                                 r->optical_batch_phase_duration_count));
                store(&r->health->optical_batch_classify_duration_avg_us,
                      (uint32_t)(r->optical_batch_classify_duration_sum_us /
                                 r->optical_batch_phase_duration_count));
            }
        }
    }
    return samples;
}

static void optical_task(void *argument)
{
    optical_runtime_t *r = (optical_runtime_t *)argument;
    r->optical_task_core = xPortGetCoreID();
    __atomic_store_n(&r->health->optical_task_core, r->optical_task_core, __ATOMIC_RELEASE);
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (process_optical_batch(r) == OPTICAL_BATCH_SAMPLES) {
            /* No delay is inserted. The bounded batch gives the scheduler a
             * task boundary; the higher-priority LED owner can preempt at all
             * ordinary points, while any raw backlog remains authoritative in
             * the SPSC ring. */
            taskYIELD();
        }
        if (nocti_v4_ads_bound_consumer_drain_requested(r->ads)) {
            /* All preceding raw items were classified by this task before the
             * ACK; ADS may now safely close its transport ownership. */
            nocti_v4_ads_bound_consumer_ack_drain(r->ads);
        }
        if (__atomic_exchange_n(&r->finish_requested, false, __ATOMIC_ACQ_REL)) {
            abort_pending(r);
            r->running = false;
            r->session_prepared = false;
            r->stopping = false;
            (void)xSemaphoreGive(r->stop_done);
        }
    }
}

static void frame_consumer_task(void *argument)
{
    optical_runtime_t *r = (optical_runtime_t *)argument;
    r->frame_task_core = xPortGetCoreID();
    __atomic_store_n(&r->health->frame_consumer_task_core, r->frame_task_core,
                     __ATOMIC_RELEASE);
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        __atomic_store_n(&r->frame_consumer_busy, true, __ATOMIC_RELEASE);
        nocti_v4_completed_optical_frame_t frame;
        while (frame_pop(r, &frame)) {
            /* A frame is 2,840 bytes. V4-6.1 copied it while holding a
             * core-0 spinlock, making this priority-5 diagnostic task
             * non-preemptible and delaying the priority-10 LED owner. The
             * latest snapshot has one writer; DUMP_FRAME is STOPPED-only and
             * also rejects while this consumer is busy, so a release-published
             * validity flag provides the required ownership boundary without
             * masking scheduling/interrupts around the bulk copy. */
            __atomic_store_n(&r->latest_frame_valid, false, __ATOMIC_RELEASE);
            r->latest_frame = frame;
            __atomic_store_n(&r->latest_frame_valid, true, __ATOMIC_RELEASE);
            if (r->completed_frame_sink != NULL) {
                (void)r->completed_frame_sink(r->completed_frame_sink_context,
                                              &frame);
            }
        }
        __atomic_store_n(&r->frame_consumer_busy, false, __ATOMIC_RELEASE);
    }
}

static void reset_optical_health(optical_runtime_t *r)
{
#define RESET(field) store(&r->health->field, 0u)
    RESET(slots_target_valid); RESET(slots_minimum_valid); RESET(slots_invalid_runtime);
    RESET(slots_aborted_by_stop); RESET(frames_total); RESET(frames_target_valid);
    RESET(frames_partial); RESET(frames_invalid_runtime); RESET(frames_aborted_by_stop);
    RESET(completed_frame_ring_overflow); RESET(completed_frame_ring_depth_max);
    RESET(representative_compute_errors);
    RESET(numeric_overflow_errors); RESET(optical_raw_processed); RESET(optical_raw_precommit);
    RESET(optical_raw_on_settle); RESET(optical_raw_on_stable); RESET(optical_raw_off_transition);
    RESET(optical_raw_off_settle); RESET(optical_raw_off_stable); RESET(optical_raw_end_guard);
    RESET(optical_raw_startup_discard); RESET(optical_raw_session_boundary);
    RESET(optical_raw_stop_boundary); RESET(optical_raw_stale_epoch); RESET(optical_raw_recovery);
    RESET(optical_raw_other_explicit); RESET(optical_raw_before_final_timing);
    RESET(optical_raw_before_on_request); RESET(optical_raw_on_request_to_submit);
    RESET(optical_raw_on_submit_to_complete); RESET(optical_raw_other_precommit);
    RESET(optical_raw_timing_gap); RESET(optical_raw_internal_error);
    RESET(optical_staging_overflow); RESET(optical_finalized_timing_backlog_overflow);
    RESET(classification_count_mismatch); RESET(classification_max_abs_on_difference);
    RESET(classification_max_abs_off_difference); RESET(optical_classification_accounting_mismatch);
    RESET(optical_timing_ring_overflow); RESET(anomaly_identity_invalid);
    RESET(optical_batches); RESET(optical_batch_samples_max);
    RESET(optical_batch_duration_max_us); RESET(optical_batch_duration_avg_us);
    RESET(optical_batch_drain_duration_max_us); RESET(optical_batch_drain_duration_avg_us);
    RESET(optical_batch_classify_duration_max_us); RESET(optical_batch_classify_duration_avg_us);
    RESET(optical_frame_emit_duration_max_us); RESET(optical_frame_emit_duration_avg_us);
    RESET(frames_startup_warmup); RESET(frames_startup_warmup_failed);
    RESET(slots_startup_warmup); RESET(raw_samples_startup_warmup);
#undef RESET
    r->optical_batch_duration_sum_us = 0u;
    r->optical_batch_duration_count = 0u;
    r->optical_batch_drain_duration_sum_us = 0u;
    r->optical_batch_classify_duration_sum_us = 0u;
    r->optical_batch_phase_duration_count = 0u;
    r->optical_frame_emit_duration_sum_us = 0u;
    r->optical_frame_emit_duration_count = 0u;
}

esp_err_t nocti_v4_optical_init(nocti_v4_optical_t *optical,
                                 nocti_v4_ads_t *ads,
                                 nocti_v4_scheduler_t *scheduler,
                                 nocti_v4_time_t *timebase,
                                 nocti_v4_health_t *health)
{
    if (optical == NULL || ads == NULL || scheduler == NULL || timebase == NULL ||
        health == NULL ||
        s_runtime_claimed) return ESP_ERR_INVALID_ARG;
    memset(optical, 0, sizeof(*optical));
    memset(&s_runtime, 0, sizeof(s_runtime));
    s_runtime_claimed = true;
    optical_runtime_t *r = &s_runtime;
    r->public_optical = optical; r->ads = ads; r->scheduler = scheduler;
    r->timebase = timebase; r->health = health;
    r->state_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    r->optical_task_core = -1; r->frame_task_core = -1;
    r->stop_done = xSemaphoreCreateBinaryStatic(&r->stop_done_storage);
    if (r->stop_done == NULL) return ESP_ERR_NO_MEM;
    r->frame_task = xTaskCreateStaticPinnedToCore(frame_consumer_task, "nocti_v4_frame",
                                                   FRAME_TASK_STACK, r, FRAME_TASK_PRIORITY,
                                                   r->frame_stack, &r->frame_task_storage,
                                                   FRAME_TASK_CORE);
    r->optical_task = xTaskCreateStaticPinnedToCore(optical_task, "nocti_v4_optical",
                                                     OPTICAL_TASK_STACK, r, OPTICAL_TASK_PRIORITY,
                                                     r->optical_stack, &r->optical_task_storage,
                                                     OPTICAL_TASK_CORE);
    if (r->frame_task == NULL || r->optical_task == NULL) return ESP_ERR_NO_MEM;
    const esp_err_t bind = nocti_v4_scheduler_bind_optical_task(scheduler, r->optical_task);
    if (bind != ESP_OK) return bind;
    optical->runtime = r; optical->initialized = true;
    return ESP_OK;
}

esp_err_t nocti_v4_optical_prepare_scan(nocti_v4_optical_t *optical)
{
    if (optical == NULL || optical->runtime == NULL) return ESP_ERR_INVALID_STATE;
    optical_runtime_t *r = (optical_runtime_t *)optical->runtime;
    if (r->session_prepared || r->running || nocti_v4_ads_is_active(r->ads)) return ESP_ERR_INVALID_STATE;
    reset_optical_health(r);
    memset(r->work, 0, sizeof(r->work)); memset(&r->builder, 0, sizeof(r->builder));
    r->last_processed_raw_ts = 0u; r->last_staged_raw_ts = 0u;
    r->raw_stage_head = 0u; r->raw_stage_tail = 0u; r->raw_stage_depth_max = 0u;
    r->finalized_timing_head = 0u; r->finalized_timing_tail = 0u;
    r->staging_integrity_fault = false; r->timing_backlog_integrity_fault = false;
    r->anomaly_count = 0u; r->anomaly_next = 0u;
    r->mismatch_count = 0u; r->mismatch_next = 0u;
    memset(r->phase_stats, 0, sizeof(r->phase_stats));
    memset(r->phase_builder, 0, sizeof(r->phase_builder));
    r->phase_model_source_off_us = 0u;
    __atomic_store_n(&r->frame_head, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&r->frame_tail, 0u, __ATOMIC_RELEASE);
    r->latest_frame_valid = false;
    const esp_err_t bind = nocti_v4_ads_bind_raw_consumer(r->ads, r->optical_task);
    if (bind != ESP_OK) return bind;
    r->session_prepared = true;
    return ESP_OK;
}

esp_err_t nocti_v4_optical_begin_scan(nocti_v4_optical_t *optical,
                                      uint32_t scheduler_session_epoch)
{
    if (optical == NULL || optical->runtime == NULL || scheduler_session_epoch == 0u) {
        return ESP_ERR_INVALID_ARG;
    }
    optical_runtime_t *r = (optical_runtime_t *)optical->runtime;
    nocti_v4_ads_status_snapshot_t status;
    nocti_v4_ads_get_status(r->ads, &status);
    if (!r->session_prepared || !nocti_v4_ads_is_active(r->ads) ||
        status.session_epoch == 0u) return ESP_ERR_INVALID_STATE;
    r->scheduler_session_epoch = scheduler_session_epoch;
    r->ads_session_epoch = status.session_epoch;
    nocti_v4_scheduler_status_t scheduler_status;
    nocti_v4_scheduler_get_status(r->scheduler, &scheduler_status);
    r->first_frame_epoch_us = scheduler_status.frame_epoch_us;
    r->running = true;
    return ESP_OK;
}

void nocti_v4_optical_begin_stop(nocti_v4_optical_t *optical)
{
    if (optical != NULL && optical->runtime != NULL) {
        optical_runtime_t *r = (optical_runtime_t *)optical->runtime;
        if (r->session_prepared) r->stopping = true;
    }
}

esp_err_t nocti_v4_optical_finish_stop(nocti_v4_optical_t *optical)
{
    if (optical == NULL || optical->runtime == NULL) return ESP_ERR_INVALID_STATE;
    optical_runtime_t *r = (optical_runtime_t *)optical->runtime;
    if (!r->session_prepared && !r->running && !r->stopping) {
        return nocti_v4_ads_bind_raw_consumer(r->ads, NULL);
    }
    (void)xSemaphoreTake(r->stop_done, 0u);
    __atomic_store_n(&r->finish_requested, true, __ATOMIC_RELEASE);
    xTaskNotifyGive(r->optical_task);
    if (xSemaphoreTake(r->stop_done, pdMS_TO_TICKS(1000u)) != pdTRUE) return ESP_ERR_TIMEOUT;
    return nocti_v4_ads_bind_raw_consumer(r->ads, NULL);
}

esp_err_t nocti_v4_optical_bind_completed_frame_sink(
    nocti_v4_optical_t *optical,
    nocti_v4_completed_frame_sink_t sink,
    void *sink_context)
{
    if (optical == NULL || optical->runtime == NULL) return ESP_ERR_INVALID_ARG;
    optical_runtime_t *r = (optical_runtime_t *)optical->runtime;
    if (r->session_prepared || r->running || r->stopping ||
        frame_depth(r) != 0u ||
        __atomic_load_n(&r->frame_consumer_busy, __ATOMIC_ACQUIRE)) {
        return ESP_ERR_INVALID_STATE;
    }
    r->completed_frame_sink = sink;
    r->completed_frame_sink_context = sink_context;
    return ESP_OK;
}

esp_err_t nocti_v4_optical_wait_frame_consumer_idle(
    nocti_v4_optical_t *optical, uint32_t timeout_ms)
{
    if (optical == NULL || optical->runtime == NULL) return ESP_ERR_INVALID_ARG;
    optical_runtime_t *r = (optical_runtime_t *)optical->runtime;
    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
    xTaskNotifyGive(r->frame_task);
    do {
        if (frame_depth(r) == 0u &&
            !__atomic_load_n(&r->frame_consumer_busy, __ATOMIC_ACQUIRE)) {
            return ESP_OK;
        }
        vTaskDelay(1u);
    } while ((xTaskGetTickCount() - start) <= timeout);
    return ESP_ERR_TIMEOUT;
}

void nocti_v4_optical_get_status(const nocti_v4_optical_t *optical,
                                 nocti_v4_optical_status_t *out_status)
{
    if (out_status == NULL) return;
    memset(out_status, 0, sizeof(*out_status));
    if (optical == NULL || optical->runtime == NULL) return;
    const optical_runtime_t *r = (const optical_runtime_t *)optical->runtime;
    out_status->initialized = optical->initialized; out_status->session_prepared = r->session_prepared;
    out_status->running = r->running; out_status->stopping = r->stopping;
    out_status->session_epoch = r->scheduler_session_epoch;
    out_status->raw_processed = r->health->optical_raw_processed;
    out_status->frame_ring_depth = frame_depth(r);
    out_status->latest_frame_valid = r->latest_frame_valid;
    out_status->optical_task_core = r->optical_task_core;
    out_status->frame_consumer_task_core = r->frame_task_core;
}

void nocti_v4_optical_print_status(const nocti_v4_optical_t *optical)
{
    nocti_v4_optical_status_t s; nocti_v4_optical_get_status(optical, &s);
    printf("FRAME_STATUS initialized=%u prepared=%u running=%u stopping=%u session=%" PRIu32
           " raw_processed=%" PRIu32 " frame_ring_depth=%" PRIu32 " latest=%u"
           " optical_core=%" PRId32 " frame_core=%" PRId32 "\n",
           s.initialized ? 1u : 0u, s.session_prepared ? 1u : 0u, s.running ? 1u : 0u,
           s.stopping ? 1u : 0u, s.session_epoch, s.raw_processed, s.frame_ring_depth,
           s.latest_frame_valid ? 1u : 0u, s.optical_task_core, s.frame_consumer_task_core);
}

void nocti_v4_optical_print_health(const nocti_v4_optical_t *optical)
{
    if (optical == NULL || optical->runtime == NULL) return;
    const optical_runtime_t *r = (const optical_runtime_t *)optical->runtime;
    const nocti_v4_health_t *h = r->health;
    printf("FRAME_HEALTH slots_target=%" PRIu32 " slots_minimum=%" PRIu32
           " slots_invalid_runtime=%" PRIu32 " slots_aborted_stop=%" PRIu32
           " warmup_frames=%" PRIu32 " warmup_slots=%" PRIu32
           " warmup_raw_samples=%" PRIu32
           " warmup_failures=%" PRIu32
           " frames_total=%" PRIu32 " frames_target=%" PRIu32 " frames_partial=%" PRIu32
           " frames_invalid_runtime=%" PRIu32 " frames_aborted_stop=%" PRIu32
           " frame_ring_overflow=%" PRIu32 " frame_ring_depth_max=%" PRIu32
           " representative_errors=%" PRIu32 " numeric_overflow=%" PRIu32
           " timing_ring_overflow=%" PRIu32 "\n",
           h->slots_target_valid, h->slots_minimum_valid, h->slots_invalid_runtime,
           h->slots_aborted_by_stop, h->frames_startup_warmup,
           h->slots_startup_warmup, h->raw_samples_startup_warmup,
           h->frames_startup_warmup_failed,
           h->frames_total, h->frames_target_valid,
           h->frames_partial, h->frames_invalid_runtime, h->frames_aborted_by_stop,
           h->completed_frame_ring_overflow,
           h->completed_frame_ring_depth_max,
           h->representative_compute_errors,
           h->numeric_overflow_errors, h->optical_timing_ring_overflow);
    const uint32_t classified = h->optical_raw_precommit + h->optical_raw_on_settle +
        h->optical_raw_on_stable + h->optical_raw_off_transition + h->optical_raw_off_settle +
        h->optical_raw_off_stable + h->optical_raw_end_guard + h->optical_raw_startup_discard +
        h->optical_raw_session_boundary + h->optical_raw_stop_boundary + h->optical_raw_stale_epoch +
        h->optical_raw_recovery + h->optical_raw_other_explicit +
        h->optical_raw_timing_gap + h->optical_raw_internal_error;
    printf("FRAME_CLASSIFY processed=%" PRIu32 " reconciled=%" PRIu32
           " precommit=%" PRIu32 " on_settle=%" PRIu32 " on_stable=%" PRIu32
           " off_transition=%" PRIu32 " off_settle=%" PRIu32 " off_stable=%" PRIu32
           " guard=%" PRIu32 " startup=%" PRIu32 " session=%" PRIu32 " stop=%" PRIu32
           " stale=%" PRIu32 " recovery=%" PRIu32 " timing_gap=%" PRIu32
           " other_explicit=%" PRIu32 " internal=%" PRIu32 "\n",
           h->optical_raw_processed, classified, h->optical_raw_precommit,
           h->optical_raw_on_settle, h->optical_raw_on_stable,
           h->optical_raw_off_transition, h->optical_raw_off_settle,
           h->optical_raw_off_stable, h->optical_raw_end_guard,
           h->optical_raw_startup_discard, h->optical_raw_session_boundary,
           h->optical_raw_stop_boundary, h->optical_raw_stale_epoch,
           h->optical_raw_recovery, h->optical_raw_timing_gap,
           h->optical_raw_other_explicit, h->optical_raw_internal_error);
    printf("FRAME_PRECOMMIT before_final_timing=%" PRIu32
           " before_on_request=%" PRIu32 " request_to_submit=%" PRIu32
           " submit_to_complete=%" PRIu32 " other_precommit=%" PRIu32
           " stage_depth_max=%" PRIu32 " stage_overflow=%" PRIu32
           " timing_backlog_overflow=%" PRIu32 "\n",
           h->optical_raw_before_final_timing, h->optical_raw_before_on_request,
           h->optical_raw_on_request_to_submit, h->optical_raw_on_submit_to_complete,
           h->optical_raw_other_precommit, r->raw_stage_depth_max,
           h->optical_staging_overflow, h->optical_finalized_timing_backlog_overflow);
    printf("FRAME_PARITY mismatch=%" PRIu32 " max_abs_on_difference=%" PRIu32
           " max_abs_off_difference=%" PRIu32 " retained=%" PRIu32
           " anomaly_identity_invalid=%" PRIu32 "\n",
           h->classification_count_mismatch, h->classification_max_abs_on_difference,
           h->classification_max_abs_off_difference, r->mismatch_count,
           h->anomaly_identity_invalid);
    printf("OPTICAL_EXEC task=nocti_v4_optical core=%" PRId32
           " priority=%u stack_bytes=%u batch_limit=%u batches=%" PRIu32
           " batch_samples_max=%" PRIu32 " batch_duration_max_us=%" PRIu32
           " batch_duration_avg_us=%" PRIu32
           " drain_max_us=%" PRIu32 " drain_avg_us=%" PRIu32
           " classify_max_us=%" PRIu32 " classify_avg_us=%" PRIu32
           " frame_emit_max_us=%" PRIu32 " frame_emit_avg_us=%" PRIu32
           " frame_task=nocti_v4_frame frame_core=%" PRId32
           " frame_priority=%u frame_stack_bytes=%u latest_copy_critical=0\n",
           r->optical_task_core, OPTICAL_TASK_PRIORITY, OPTICAL_TASK_STACK,
           OPTICAL_BATCH_SAMPLES, h->optical_batches,
           h->optical_batch_samples_max, h->optical_batch_duration_max_us,
           h->optical_batch_duration_avg_us,
           h->optical_batch_drain_duration_max_us,
           h->optical_batch_drain_duration_avg_us,
           h->optical_batch_classify_duration_max_us,
           h->optical_batch_classify_duration_avg_us,
           h->optical_frame_emit_duration_max_us,
           h->optical_frame_emit_duration_avg_us, r->frame_task_core,
           FRAME_TASK_PRIORITY, FRAME_TASK_STACK);
}

void nocti_v4_optical_dump_latest_frame(const nocti_v4_optical_t *optical)
{
    if (optical == NULL || optical->runtime == NULL) return;
    optical_runtime_t *r = (optical_runtime_t *)optical->runtime;
    if (r->running || r->session_prepared) {
        printf("DUMP_FRAME REJECTED reason=SCAN_ACTIVE; run SCAN_STOP first\n");
        return;
    }
    if (frame_depth(r) != 0u ||
        __atomic_load_n(&r->frame_consumer_busy, __ATOMIC_ACQUIRE)) {
        printf("DUMP_FRAME RETRY reason=FRAME_CONSUMER_PENDING\n");
        return;
    }
    /* Command-context only: this diagnostic scan is never in an ADS, I2C,
     * scheduler, or callback path. It makes the repaired main-stack margin
     * observable without reintroducing a frame-sized automatic object. */
    const UBaseType_t main_stack_high_water = uxTaskGetStackHighWaterMark(NULL);
    const bool valid = __atomic_load_n(&r->latest_frame_valid,
                                       __ATOMIC_ACQUIRE);
    if (!valid) { printf("DUMP_FRAME state=NO_COMPLETED_FRAME\n"); return; }
    /* This is intentionally a pointer to persistent diagnostic storage. A
     * completed frame is several KiB; copying it into the 3584-byte main task
     * stack caused the confirmed V4-6 reset. SCAN is stopped and the frame
     * consumer is idle above, so the persistent snapshot cannot be replaced. */
    const nocti_v4_completed_optical_frame_t *frame = &r->latest_frame;
    printf("DUMP_FRAME main_stack_high_water_units=%" PRIu32
           " session=%" PRIu32 " frame=%" PRIu32 " nominal=%" PRIu64
           " complete=%" PRIu64 " quality=%u target_slots=%u minimum_slots=%u invalid_slots=%u reason=0x%08" PRIX32 "\n",
           (uint32_t)main_stack_high_water,
           frame->session_epoch, frame->frame_sequence, frame->nominal_frame_timestamp_us,
           frame->completed_timestamp_us, (unsigned)frame->quality, frame->target_valid_slots,
           frame->minimum_valid_slots, frame->invalid_slots, frame->reason);
    for (uint8_t slot = 0u; slot < NOCTI_V4_SLOT_COUNT; ++slot) {
        const nocti_v4_optical_slot_value_t *v = &frame->slot[slot];
        printf("FRAME_SLOT s=%u src=%u geom=%u wl=%u pca=%u n_on=%u/%u n_off=%u/%u q=%u reason=0x%08" PRIX32
               " ION=%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRId32
               " IOFF=%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRId32
               " ICORR=%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 "\n",
               v->slot_index, v->logical_source, v->geometry_id, v->wavelength_nm, v->pca_channel,
               v->n_on_available, v->n_on_used, v->n_off_available, v->n_off_used, (unsigned)v->quality,
               v->reason, v->i_on[0], v->i_on[1], v->i_on[2], v->i_on[3], v->i_led_off[0],
               v->i_led_off[1], v->i_led_off[2], v->i_led_off[3], v->i_corrected[0],
               v->i_corrected[1], v->i_corrected[2], v->i_corrected[3]);
    }
}

void nocti_v4_optical_dump_k_anomalies(const nocti_v4_optical_t *optical)
{
    if (optical == NULL || optical->runtime == NULL) return;
    const optical_runtime_t *r = (const optical_runtime_t *)optical->runtime;
    printf("DUMP_K_ANOMALIES retained=%" PRIu32 " capacity=%u\n", r->anomaly_count,
           NOCTI_V4_K_ANOMALY_RING_CAPACITY);
    const uint32_t first = r->anomaly_next > r->anomaly_count ? r->anomaly_next - r->anomaly_count : 0u;
    for (uint32_t index = 0u; index < r->anomaly_count; ++index) {
        const nocti_v4_k_anomaly_t *a = &r->anomaly[(first + index) % NOCTI_V4_K_ANOMALY_RING_CAPACITY];
        printf("K_ANOMALY session=%" PRIu32 " frame=%" PRIu32
               " slot_epoch=%" PRIu32 " slot=%u n_on=%u n_off=%u"
               " sched_late=%" PRIu32 " request_to_submit=%" PRIu32
               " on_latency=%" PRIu32 " off_latency=%" PRIu32 "\n",
               a->session_epoch, a->frame_sequence, a->slot_epoch,
               a->slot_index, a->n_on, a->n_off,
               a->scheduler_lateness_us, a->led_request_to_submit_us,
               a->led_on_latency_us, a->led_off_latency_us);
    }
}

void nocti_v4_optical_dump_classify_mismatches(const nocti_v4_optical_t *optical)
{
    if (optical == NULL || optical->runtime == NULL) return;
    const optical_runtime_t *r = (const optical_runtime_t *)optical->runtime;
    const nocti_v4_health_t *h = r->health;
    printf("DUMP_CLASSIFY_MISMATCH total=%" PRIu32 " retained=%" PRIu32
           " capacity=%u max_abs_on=%" PRIu32 " max_abs_off=%" PRIu32 "\n",
           h->classification_count_mismatch, r->mismatch_count,
           NOCTI_V4_CLASSIFY_MISMATCH_RING_CAPACITY,
           h->classification_max_abs_on_difference,
           h->classification_max_abs_off_difference);
    const uint32_t first = r->mismatch_next > r->mismatch_count ?
        r->mismatch_next - r->mismatch_count : 0u;
    for (uint32_t index = 0u; index < r->mismatch_count; ++index) {
        const nocti_v4_classify_mismatch_t *m =
            &r->mismatch[(first + index) % NOCTI_V4_CLASSIFY_MISMATCH_RING_CAPACITY];
        printf("CLASSIFY_MISMATCH session=%" PRIu32 " frame=%" PRIu32 " slot=%u"
               " v5_on=%u v5_off=%u v6_on=%u v6_off=%u diff_on=%d diff_off=%d\n",
               m->session_epoch, m->frame_sequence, m->slot_index,
               m->v5_candidate_on_equivalent, m->v5_candidate_off_equivalent,
               m->v6_n_on_available, m->v6_n_off_available,
               m->difference_on, m->difference_off);
    }
}

void nocti_v4_optical_print_phase_sweep(const nocti_v4_optical_t *optical)
{
    if (optical == NULL || optical->runtime == NULL) {
        printf("TIMING_PHASE_SWEEP state=NOT_INITIALIZED\n");
        return;
    }
    const optical_runtime_t *r =
        (const optical_runtime_t *)optical->runtime;
    printf("TIMING_PHASE_SWEEP classification=COUNTERFACTUAL_MODELLED"
           " physical_off_deadline_us=%" PRIu32 " physical_timing_unchanged=1"
           " source=actual_finalized_slot_timing_plus_actual_drdy_timestamps"
           " assumption=measured_off_request_and_complete_shift_with_deadline"
           " live_snapshot_may_differ_by_one_slot=1\n",
           r->phase_model_source_off_us);
    for (uint8_t index = 0u; index < PHASE_SWEEP_COUNT; ++index) {
        const phase_model_stats_t *stats = &r->phase_stats[index];
        const uint32_t slots = load(&stats->slots);
        const uint32_t on_sum = load(&stats->on_sum);
        const uint32_t off_sum = load(&stats->off_sum);
        const uint64_t on_mean_milli = slots == 0u ? 0u :
            ((uint64_t)on_sum * 1000u) / slots;
        const uint64_t off_mean_milli = slots == 0u ? 0u :
            ((uint64_t)off_sum * 1000u) / slots;
        printf("PHASE_MODEL off_deadline_us=%u slots=%" PRIu32
               " ON_k_0_7=%" PRIu32 " ON_k_8_9=%" PRIu32
               " ON_k_ge10=%" PRIu32 " ON_mean=%" PRIu64 ".%03" PRIu64
               " OFF_k_0_7=%" PRIu32 " OFF_k_8_9=%" PRIu32
               " OFF_k_ge10=%" PRIu32 " OFF_mean=%" PRIu64 ".%03" PRIu64
               " FRAME_target=%" PRIu32 " FRAME_partial=%" PRIu32
               " FRAME_invalid=%" PRIu32 "\n",
               s_phase_sweep_off_us[index], slots,
               load(&stats->on_0_to_7), load(&stats->on_8_to_9),
               load(&stats->on_ge10), on_mean_milli / 1000u,
               on_mean_milli % 1000u,
               load(&stats->off_0_to_7), load(&stats->off_8_to_9),
               load(&stats->off_ge10), off_mean_milli / 1000u,
               off_mean_milli % 1000u,
               load(&stats->frames_target), load(&stats->frames_partial),
               load(&stats->frames_invalid));
    }
}

void nocti_v4_optical_dump_map(void)
{
    printf("DUMP_OPTICAL_MAP method=V2_latest_contiguous_integrity_valid_tail_mean K_target=10 K_min=8 channels=ADS0,ADS1,ADS2,ADS3\n");
    for (uint8_t slot = 0u; slot < NOCTI_V4_SLOT_COUNT; ++slot) {
        const nocti_v4_optical_slot_descriptor_t *d = &s_descriptor[slot];
        printf("OPTICAL_MAP slot=%u logical_source=%u geometry=%u wavelength_nm=%u pca_channel=%u"
               " detectors=ADS0,ADS1,ADS2,ADS3 pair_slots=%u/%u"
               " host_channel_identity=NOT_DEFINED_IN_V2_V3_SOURCE\n",
               d->slot_index, d->logical_source, d->geometry_id, d->wavelength_nm,
               d->pca_channel, (unsigned)(d->geometry_id * 2u),
               (unsigned)(d->geometry_id * 2u + 1u));
    }
}

void nocti_v4_optical_sample_stack_watermarks(nocti_v4_optical_t *optical)
{
    if (optical == NULL || optical->runtime == NULL) return;
    optical_runtime_t *r = (optical_runtime_t *)optical->runtime;
    store(&r->health->optical_task_stack_watermark_bytes,
          uxTaskGetStackHighWaterMark(r->optical_task));
    store(&r->health->frame_consumer_task_stack_watermark_bytes,
          uxTaskGetStackHighWaterMark(r->frame_task));
}
