#pragma once

#include <stdint.h>

/* Slot geometry remains immutable. The OFF phase is selected only while
 * STOPPED, then snapshotted immutably for a complete SCAN session. */
#define NOCTI_V4_SLOT_COUNT                 20u
/* One stopped-only setting shared by scheduler, optical timestamps and wire
 * metadata. Default 10 Hz preserves historical V4 geometry exactly. */
uint32_t nocti_v4_scheduler_slot_period_us(void);
#define NOCTI_V4_SLOT_PERIOD_US             (nocti_v4_scheduler_slot_period_us())
#define NOCTI_V4_FRAME_PERIOD_US            (NOCTI_V4_SLOT_COUNT * NOCTI_V4_SLOT_PERIOD_US)
#define NOCTI_V4_OFF_PHASE_PRECOMP_US       (NOCTI_V4_SLOT_PERIOD_US * 44u / 100u)
#define NOCTI_V4_OFF_PHASE_DEFAULT_US       NOCTI_V4_OFF_PHASE_PRECOMP_US
#define NOCTI_V4_OFF_PHASE_QUALIFICATION_US (NOCTI_V4_SLOT_PERIOD_US / 2u)
/* Compatibility name denotes the boot/default phase, not the active session
 * phase. Runtime code must use the scheduler's snapshotted value. */
#define NOCTI_V4_LED_ON_BUDGET_US           NOCTI_V4_OFF_PHASE_DEFAULT_US
#define NOCTI_V4_LED_ON_SETTLE_US           500u
#define NOCTI_V4_LED_OFF_SETTLE_US          500u
#define NOCTI_V4_SLOT_END_GUARD_US          100u
#define NOCTI_V4_K_ON_TARGET                10u
#define NOCTI_V4_K_OFF_TARGET               10u
#define NOCTI_V4_K_ON_MINIMUM               8u
#define NOCTI_V4_K_OFF_MINIMUM              8u

#define NOCTI_V4_OFF_PHASE_IS_SUPPORTED(value) \
    ((value) == NOCTI_V4_OFF_PHASE_DEFAULT_US || \
     (value) == NOCTI_V4_OFF_PHASE_QUALIFICATION_US)

/* A diagnostic threshold only; a late event remains anchored to its immutable
 * deadline and is counted rather than shifting subsequent slots. */
#define NOCTI_V4_SCHEDULER_MISSED_DEADLINE_US 1000u
#define NOCTI_V4_SCHEDULER_ARM_LEAD_US        NOCTI_V4_SLOT_PERIOD_US
#define NOCTI_V4_SCHEDULER_EVENT_RING_CAPACITY 8u
#define NOCTI_V4_SCHEDULER_FINALIZED_RING_CAPACITY 32u
#define NOCTI_V4_SCHEDULER_HISTORY_FRAMES     3u
#include "nocti_v4_rtos.h"

/* Core 1: ADS 23 preempts LED 22, which preempts this scheduler. Post-ON
 * bookkeeping therefore cannot run ahead of a pending DRDY service. The
 * handoff itself is still a nonblocking queue send of a prebuilt command. */
#define NOCTI_V4_SCHEDULER_PRIORITY           NOCTI_V4_RTOS_PRIO_SCHED
#define NOCTI_V4_SCHEDULER_CORE               NOCTI_V4_RTOS_CORE_ACQ
#define NOCTI_V4_SCHEDULER_STACK_BYTES        NOCTI_V4_RTOS_STACK_SCHED_BYTES
