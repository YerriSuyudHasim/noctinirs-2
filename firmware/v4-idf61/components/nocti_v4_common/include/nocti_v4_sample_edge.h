#pragma once

/* ISR-to-scheduler sample-edge wake. Not a second clock and not an I2C path.
 * Host tests do not include this header. See docs/V4_IDF61_SINGLE_CLOCK.md.
 */

#include <stdint.h>

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nocti_v4_sample_clock.h"

typedef struct {
    volatile uint32_t generation;
    volatile uint32_t have_submit;
    volatile uint32_t wake_pending;
    volatile uint32_t next_submit_sequence;
    volatile uint32_t observed_sequence;
    volatile uint32_t observed_generation;
    volatile uint32_t observed_valid;
    volatile uint32_t observed_us_lo;
    volatile uint32_t observed_us_hi;
    volatile uint32_t watch_sequence;
    volatile uint32_t watch_armed;
    volatile uint32_t latched_sequence;
    volatile uint32_t latch_valid;
    volatile uint32_t latched_us_lo;
    volatile uint32_t latched_us_hi;
    TaskHandle_t task;
} nocti_v4_sample_edge_state_t;

extern nocti_v4_sample_edge_state_t nocti_v4_sample_edge_state;

void nocti_v4_sample_edge_arm(TaskHandle_t task, uint32_t generation);
void nocti_v4_sample_edge_disarm(void);
void nocti_v4_sample_edge_set_submit(uint32_t sequence);
void nocti_v4_sample_edge_set_watch(uint32_t sequence);
int nocti_v4_sample_edge_observed(uint32_t *sequence, uint32_t *generation);
int nocti_v4_sample_edge_copy_latch(uint32_t *sequence, uint64_t *timestamp_us);

/* Called from the DRDY ISR only. Atomically publishes the index and, when a
 * submit is due, wakes the scheduler. No SPI, I2C, or logging. */
static inline void __attribute__((always_inline)) IRAM_ATTR nocti_v4_sample_edge_on_drdy(
    uint32_t sequence, uint64_t timestamp_us, BaseType_t *higher_priority_woken)
{
    nocti_v4_sample_edge_state_t *edge = &nocti_v4_sample_edge_state;
    const uint32_t generation = __atomic_load_n(&edge->generation, __ATOMIC_ACQUIRE);
    TaskHandle_t task;
    if (generation == 0u) {
        return;
    }
    __atomic_store_n(&edge->observed_sequence, sequence, __ATOMIC_RELAXED);
    __atomic_store_n(&edge->observed_us_lo, (uint32_t)timestamp_us, __ATOMIC_RELAXED);
    __atomic_store_n(&edge->observed_us_hi, (uint32_t)(timestamp_us >> 32), __ATOMIC_RELAXED);
    __atomic_store_n(&edge->observed_generation, generation, __ATOMIC_RELEASE);
    __atomic_store_n(&edge->observed_valid, 1u, __ATOMIC_RELEASE);
    if (__atomic_load_n(&edge->watch_armed, __ATOMIC_RELAXED) != 0u &&
        sequence == __atomic_load_n(&edge->watch_sequence, __ATOMIC_RELAXED)) {
        __atomic_store_n(&edge->latched_sequence, sequence, __ATOMIC_RELAXED);
        __atomic_store_n(&edge->latched_us_lo, (uint32_t)timestamp_us, __ATOMIC_RELAXED);
        __atomic_store_n(&edge->latched_us_hi, (uint32_t)(timestamp_us >> 32),
                         __ATOMIC_RELAXED);
        __atomic_store_n(&edge->latch_valid, 1u, __ATOMIC_RELEASE);
        __atomic_store_n(&edge->watch_armed, 0u, __ATOMIC_RELEASE);
    }
    task = __atomic_load_n(&edge->task, __ATOMIC_ACQUIRE);
    if (task == NULL || higher_priority_woken == NULL) {
        return;
    }
    if (__atomic_load_n(&edge->wake_pending, __ATOMIC_ACQUIRE) == 0u &&
        (__atomic_load_n(&edge->have_submit, __ATOMIC_ACQUIRE) == 0u ||
        nocti_v4_sample_reached(
            sequence, __atomic_load_n(&edge->next_submit_sequence, __ATOMIC_RELAXED)))) {
        __atomic_store_n(&edge->wake_pending, 1u, __ATOMIC_RELEASE);
        vTaskNotifyGiveFromISR(task, higher_priority_woken);
    }
}
