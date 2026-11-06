#pragma once

/* Host-testable ADS131M04 sample-index clock.
 *
 * The DRDY index is the master. GPTimer microseconds are not an input to
 * geometry, edge placement, or classification. See docs/V4_IDF61_SINGLE_CLOCK.md.
 * This header does not include ESP-IDF or FreeRTOS.
 */

#include <stdint.h>

#define NOCTI_V4_SAMPLE_ADC_HZ 8000u
#define NOCTI_V4_SAMPLE_PERIOD_US 125u
#define NOCTI_V4_SAMPLE_SLOT_COUNT 20u
#define NOCTI_V4_SAMPLE_FILTER_SETTLE 3u
#define NOCTI_V4_SAMPLE_FRONTEND_PLACEHOLDER 1u
#define NOCTI_V4_SAMPLE_SETTLE \
    (NOCTI_V4_SAMPLE_FILTER_SETTLE + NOCTI_V4_SAMPLE_FRONTEND_PLACEHOLDER)
#define NOCTI_V4_SAMPLE_GUARD 1u
/* Two samples: a commit faster than one period lands in the one-sample guard,
 * not in the previous stable window. 250 us covers the V3 I2C allowance. */
#define NOCTI_V4_SAMPLE_LED_LEAD 2u
#define NOCTI_V4_SAMPLE_K_TARGET 10u
#define NOCTI_V4_SAMPLE_K_MIN 8u
#define NOCTI_V4_SAMPLE_GROUP_DELAY_HALVES 3u

_Static_assert(NOCTI_V4_SAMPLE_SETTLE == 4u, "sinc3 plus one placeholder sample");
_Static_assert(NOCTI_V4_SAMPLE_LED_LEAD == 2u, "lead must match the design doc");
_Static_assert(NOCTI_V4_SAMPLE_GUARD == 1u, "100 us guard quantizes to one sample");

typedef enum {
    NOCTI_V4_SAMPLE_CLASS_OUTSIDE = 0,
    NOCTI_V4_SAMPLE_CLASS_PRECOMMIT,
    NOCTI_V4_SAMPLE_CLASS_COMMIT_LATE,
    NOCTI_V4_SAMPLE_CLASS_ON_SETTLE,
    NOCTI_V4_SAMPLE_CLASS_ON_STABLE,
    NOCTI_V4_SAMPLE_CLASS_OFF_TRANSITION,
    NOCTI_V4_SAMPLE_CLASS_OFF_SETTLE,
    NOCTI_V4_SAMPLE_CLASS_OFF_STABLE,
    NOCTI_V4_SAMPLE_CLASS_END_GUARD
} nocti_v4_sample_class_t;

typedef struct {
    uint32_t optical_fs_hz;
    uint32_t adc_hz;
    uint32_t slot_us;
    uint32_t off_phase_us;
    uint32_t slot_samples;
    uint32_t frame_samples;
    uint32_t off_phase_samples;
    uint32_t settle_samples;
    uint32_t guard_samples;
    uint32_t lead_samples;
    uint32_t on_stable_samples;
    uint32_t off_stable_samples;
    uint8_t valid;
} nocti_v4_sample_geometry_t;

typedef struct {
    uint32_t on_edge;
    uint32_t off_edge;
    uint32_t slot_end;
    uint32_t guard_start;
    uint32_t on_submit;
    uint32_t off_submit;
    uint32_t on_first_affected;
    uint32_t off_first_affected;
    uint8_t on_commit_known;
    uint8_t off_commit_known;
    uint8_t on_commit_late;
    uint8_t off_commit_late;
    uint32_t on_late_samples;
    uint32_t off_late_samples;
} nocti_v4_sample_slot_plan_t;

typedef struct {
    uint32_t on_stable;
    uint32_t off_stable;
    uint32_t commit_late;
    uint32_t excluded;
} nocti_v4_sample_k_count_t;

/* Round half up onto the 125 us grid. Exact multiples stay put. */
static inline uint32_t nocti_v4_sample_quantize_us(uint32_t us)
{
    return (us + (NOCTI_V4_SAMPLE_PERIOD_US / 2u)) / NOCTI_V4_SAMPLE_PERIOD_US;
}

/* Signed serial compare. Valid while the true distance is inside 2^31. */
static inline int nocti_v4_sample_reached(uint32_t now, uint32_t target)
{
    return (int32_t)(now - target) >= 0;
}

static inline uint32_t nocti_v4_sample_since(uint32_t earlier, uint32_t later)
{
    return later - earlier;
}

static inline int nocti_v4_sample_fs_allowed(uint32_t optical_fs_hz)
{
    return optical_fs_hz == 2u || optical_fs_hz == 4u || optical_fs_hz == 5u ||
           optical_fs_hz == 8u || optical_fs_hz == 10u;
}

static inline uint32_t nocti_v4_sample_later(uint32_t left, uint32_t right)
{
    return nocti_v4_sample_reached(left, right) ? left : right;
}

/* off_phase_us is the scheduler's frozen microsecond phase (44% or 50%).
 * Returns 1 and fills out on success. Returns 0 if fs, grid, or K_min fails. */
static inline int nocti_v4_sample_geometry_for(uint32_t optical_fs_hz,
                                               uint32_t off_phase_us,
                                               nocti_v4_sample_geometry_t *out)
{
    nocti_v4_sample_geometry_t geo;
    uint32_t slot_us;
    uint32_t slot_samples;
    uint32_t off_samples;
    uint32_t on_stable;
    uint32_t off_stable;
    if (out == NULL) {
        return 0;
    }
    geo.valid = 0u;
    *out = geo;
    if (!nocti_v4_sample_fs_allowed(optical_fs_hz) || off_phase_us == 0u) {
        return 0;
    }
    slot_us = 1000000u / (NOCTI_V4_SAMPLE_SLOT_COUNT * optical_fs_hz);
    if (slot_us * NOCTI_V4_SAMPLE_SLOT_COUNT * optical_fs_hz != 1000000u ||
        (slot_us % NOCTI_V4_SAMPLE_PERIOD_US) != 0u ||
        off_phase_us >= slot_us) {
        return 0;
    }
    slot_samples = slot_us / NOCTI_V4_SAMPLE_PERIOD_US;
    off_samples = nocti_v4_sample_quantize_us(off_phase_us);
    if (off_samples <= NOCTI_V4_SAMPLE_SETTLE ||
        off_samples >= slot_samples - NOCTI_V4_SAMPLE_SETTLE - NOCTI_V4_SAMPLE_GUARD ||
        off_samples <= NOCTI_V4_SAMPLE_LED_LEAD ||
        slot_samples - off_samples <= NOCTI_V4_SAMPLE_LED_LEAD) {
        return 0;
    }
    on_stable = off_samples - NOCTI_V4_SAMPLE_SETTLE;
    off_stable = slot_samples - off_samples - NOCTI_V4_SAMPLE_SETTLE -
                 NOCTI_V4_SAMPLE_GUARD;
    if (on_stable < NOCTI_V4_SAMPLE_K_MIN || off_stable < NOCTI_V4_SAMPLE_K_MIN) {
        return 0;
    }
    geo.optical_fs_hz = optical_fs_hz;
    geo.adc_hz = NOCTI_V4_SAMPLE_ADC_HZ;
    geo.slot_us = slot_us;
    geo.off_phase_us = off_phase_us;
    geo.slot_samples = slot_samples;
    geo.frame_samples = slot_samples * NOCTI_V4_SAMPLE_SLOT_COUNT;
    geo.off_phase_samples = off_samples;
    geo.settle_samples = NOCTI_V4_SAMPLE_SETTLE;
    geo.guard_samples = NOCTI_V4_SAMPLE_GUARD;
    geo.lead_samples = NOCTI_V4_SAMPLE_LED_LEAD;
    geo.on_stable_samples = on_stable;
    geo.off_stable_samples = off_stable;
    geo.valid = 1u;
    *out = geo;
    return 1;
}

/* frame_index is 0-based. slot_index is 0..19. origin is slot 0 of frame 0. */
static inline int nocti_v4_sample_slot_plan(const nocti_v4_sample_geometry_t *geo,
                                            uint32_t origin, uint32_t frame_index,
                                            uint32_t slot_index,
                                            nocti_v4_sample_slot_plan_t *out)
{
    nocti_v4_sample_slot_plan_t plan;
    uint32_t on_edge;
    if (out == NULL) {
        return 0;
    }
    plan = (nocti_v4_sample_slot_plan_t){0};
    *out = plan;
    if (geo == NULL || geo->valid == 0u || slot_index >= NOCTI_V4_SAMPLE_SLOT_COUNT) {
        return 0;
    }
    on_edge = origin + frame_index * geo->frame_samples + slot_index * geo->slot_samples;
    plan.on_edge = on_edge;
    plan.off_edge = on_edge + geo->off_phase_samples;
    plan.slot_end = on_edge + geo->slot_samples;
    plan.guard_start = plan.slot_end - geo->guard_samples;
    plan.on_submit = on_edge - geo->lead_samples;
    plan.off_submit = plan.off_edge - geo->lead_samples;
    *out = plan;
    return 1;
}

/* commit_sequence is the last DRDY already published when I2C completed.
 * The first conversion that can see the new level is the next index. */
static inline void nocti_v4_sample_note_commit(nocti_v4_sample_slot_plan_t *plan,
                                               int on_phase,
                                               uint32_t commit_sequence)
{
    uint32_t nominal;
    uint32_t first;
    uint32_t late;
    if (plan == NULL) {
        return;
    }
    nominal = on_phase ? plan->on_edge : plan->off_edge;
    first = commit_sequence + 1u;
    late = nocti_v4_sample_reached(first, nominal) ? (first - nominal) : 0u;
    if (on_phase) {
        plan->on_commit_known = 1u;
        plan->on_first_affected = first;
        plan->on_commit_late = late > 0u ? 1u : 0u;
        plan->on_late_samples = late;
    } else {
        plan->off_commit_known = 1u;
        plan->off_first_affected = first;
        plan->off_commit_late = late > 0u ? 1u : 0u;
        plan->off_late_samples = late;
    }
}

/* One class per sample. Early commits do not pull the new phase earlier than
 * its nominal edge. Late commits blank [edge, first_affected) as COMMIT_LATE
 * on the ON side and as OFF_TRANSITION on the OFF side. */
static inline nocti_v4_sample_class_t nocti_v4_sample_classify(
    const nocti_v4_sample_geometry_t *geo,
    const nocti_v4_sample_slot_plan_t *plan, uint32_t sequence)
{
    uint32_t on_limit;
    uint32_t on_accept;
    uint32_t off_accept;
    if (geo == NULL || geo->valid == 0u || plan == NULL ||
        !nocti_v4_sample_reached(sequence, plan->on_edge) ||
        nocti_v4_sample_reached(sequence, plan->slot_end)) {
        return NOCTI_V4_SAMPLE_CLASS_OUTSIDE;
    }
    if (nocti_v4_sample_reached(sequence, plan->guard_start)) {
        return NOCTI_V4_SAMPLE_CLASS_END_GUARD;
    }
    on_limit = plan->off_edge;
    if (plan->off_commit_known != 0u &&
        nocti_v4_sample_reached(plan->off_edge, plan->off_first_affected) &&
        plan->off_first_affected != plan->off_edge) {
        on_limit = plan->off_first_affected;
    }
    if (!nocti_v4_sample_reached(sequence, on_limit)) {
        if (plan->on_commit_known == 0u ||
            !nocti_v4_sample_reached(sequence, plan->on_first_affected)) {
            if (plan->on_commit_known != 0u && plan->on_commit_late != 0u) {
                return NOCTI_V4_SAMPLE_CLASS_COMMIT_LATE;
            }
            return NOCTI_V4_SAMPLE_CLASS_PRECOMMIT;
        }
        on_accept = nocti_v4_sample_later(plan->on_edge + geo->settle_samples,
                                          plan->on_first_affected + geo->settle_samples);
        if (!nocti_v4_sample_reached(sequence, on_accept)) {
            return NOCTI_V4_SAMPLE_CLASS_ON_SETTLE;
        }
        return NOCTI_V4_SAMPLE_CLASS_ON_STABLE;
    }
    if (!nocti_v4_sample_reached(sequence, plan->off_edge)) {
        return NOCTI_V4_SAMPLE_CLASS_OFF_TRANSITION;
    }
    if (plan->off_commit_known == 0u ||
        !nocti_v4_sample_reached(sequence, plan->off_first_affected)) {
        return NOCTI_V4_SAMPLE_CLASS_OFF_TRANSITION;
    }
    off_accept = nocti_v4_sample_later(plan->off_edge + geo->settle_samples,
                                       plan->off_first_affected + geo->settle_samples);
    if (!nocti_v4_sample_reached(sequence, off_accept)) {
        return NOCTI_V4_SAMPLE_CLASS_OFF_SETTLE;
    }
    return NOCTI_V4_SAMPLE_CLASS_OFF_STABLE;
}

static inline void nocti_v4_sample_count_slot(const nocti_v4_sample_geometry_t *geo,
                                              const nocti_v4_sample_slot_plan_t *plan,
                                              nocti_v4_sample_k_count_t *out)
{
    uint32_t sequence;
    uint32_t guard;
    nocti_v4_sample_k_count_t count = {0, 0, 0, 0};
    if (out == NULL) {
        return;
    }
    *out = count;
    if (geo == NULL || plan == NULL || geo->valid == 0u) {
        return;
    }
    sequence = plan->on_edge;
    guard = 0u;
    while (!nocti_v4_sample_reached(sequence, plan->slot_end) &&
           guard < geo->slot_samples) {
        switch (nocti_v4_sample_classify(geo, plan, sequence)) {
        case NOCTI_V4_SAMPLE_CLASS_ON_STABLE:
            count.on_stable += 1u;
            break;
        case NOCTI_V4_SAMPLE_CLASS_OFF_STABLE:
            count.off_stable += 1u;
            break;
        case NOCTI_V4_SAMPLE_CLASS_COMMIT_LATE:
            count.commit_late += 1u;
            count.excluded += 1u;
            break;
        default:
            count.excluded += 1u;
            break;
        }
        sequence += 1u;
        guard += 1u;
    }
    *out = count;
}

static inline void nocti_v4_sample_store_u64_le(uint8_t out[8], uint64_t value)
{
    uint8_t index;
    if (out == NULL) {
        return;
    }
    for (index = 0u; index < 8u; ++index) {
        out[index] = (uint8_t)(value >> (8u * index));
    }
}

static inline uint64_t nocti_v4_sample_load_u64_le(const uint8_t in[8])
{
    uint64_t value = 0u;
    uint8_t index;
    if (in == NULL) {
        return 0u;
    }
    for (index = 0u; index < 8u; ++index) {
        value |= (uint64_t)in[index] << (8u * index);
    }
    return value;
}

/* Nominal host microsecond from the sample index. Not an ESP crystal reading. */
static inline uint64_t nocti_v4_sample_host_us(uint64_t session_epoch_us,
                                               uint32_t origin, uint32_t sequence)
{
    return session_epoch_us +
           (uint64_t)nocti_v4_sample_since(origin, sequence) * NOCTI_V4_SAMPLE_PERIOD_US;
}
