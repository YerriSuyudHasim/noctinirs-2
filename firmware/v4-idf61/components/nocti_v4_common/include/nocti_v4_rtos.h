#pragma once

/* Single Core-1 / Core-0 ladder for the V4-on-IDF-6.1 image.
 * Numbers are an engineering response to boot82 (EVENT_BACKLOG, depth 4,
 * missed 3, SPI 32-45 us, CRC/SPI/status all 0). They are not a Class H lock
 * and they do not change ADR-0003's open NN2 cells. */

/* Acquisition core. Wi-Fi, lwIP and the frame consumer stay on core 0. */
#define NOCTI_V4_RTOS_CORE_ACQ 1
#define NOCTI_V4_RTOS_CORE_SOFT 0

/* Core 1, highest first.
 * ADS must preempt LED I2C and scheduler bookkeeping. A DRDY period is 125 us
 * and a measured SPI frame is 32-45 us. A slot edge is milliseconds, and the
 * LED settle budget is 500 us, so one preempting SPI fits in that budget.
 * edb7041 used ADS 23 above LED 22. df796ba and this restart's previous image
 * put ADS back at 20, below scheduler 21 and LED 22. That order can hold the
 * CPU across several DRDY periods. */
#define NOCTI_V4_RTOS_PRIO_ADS 23u
#define NOCTI_V4_RTOS_PRIO_LED 22u
#define NOCTI_V4_RTOS_PRIO_SCHED 15u
/* Setup/stop worker. Blocking I2C only while stopped or stopping. It stays on
 * core 1 so the PCA driver keeps allocating its interrupt away from Wi-Fi,
 * and it stays below the acquisition ladder. */
#define NOCTI_V4_RTOS_PRIO_CONTROL 3u

/* Core 0, highest first. The IDF Wi-Fi task remains above this band. */
#define NOCTI_V4_RTOS_PRIO_OPTICAL 9u
#define NOCTI_V4_RTOS_PRIO_FRAME 5u
#define NOCTI_V4_RTOS_PRIO_NET 4u
#define NOCTI_V4_RTOS_PRIO_DARK 2u

/* Stacks are bytes, passed to xTaskCreate* as ESP-IDF byte counts.
 * Shrink only after nn2ctl timing reports a high-water mark. */
#define NOCTI_V4_RTOS_STACK_ADS_BYTES 4096u
#define NOCTI_V4_RTOS_STACK_LED_BYTES 4096u
#define NOCTI_V4_RTOS_STACK_SCHED_BYTES 6144u
#define NOCTI_V4_RTOS_STACK_OPTICAL_BYTES 6144u
#define NOCTI_V4_RTOS_STACK_FRAME_BYTES 4096u
#define NOCTI_V4_RTOS_STACK_DARK_BYTES 4096u
#define NOCTI_V4_RTOS_STACK_CONTROL_BYTES 8192u
#define NOCTI_V4_RTOS_STACK_NET_BYTES 16384u

/* Time budgets. A budget is the runnable CPU window, not the I2C bus time.
 * The LED task blocks on the completion notification; the bus transaction
 * continues under the I2C ISR and may be sliced by one ADS SPI. */
#define NOCTI_V4_RTOS_BUDGET_DRDY_PERIOD_US 125u
#define NOCTI_V4_RTOS_BUDGET_ADS_SERVICE_US 80u
#define NOCTI_V4_RTOS_BUDGET_LED_SUBMIT_US 40u
#define NOCTI_V4_RTOS_BUDGET_SCHED_HANDOFF_US 50u
/* Idle wait used to kick the task watchdog. The SDK TWDT timeout is 5 s. */
#define NOCTI_V4_RTOS_WDT_IDLE_WAIT_MS 1000u

_Static_assert(NOCTI_V4_RTOS_PRIO_ADS > NOCTI_V4_RTOS_PRIO_LED,
               "ADS must preempt the LED owner");
_Static_assert(NOCTI_V4_RTOS_PRIO_LED > NOCTI_V4_RTOS_PRIO_SCHED,
               "LED handoff must preempt scheduler bookkeeping");
_Static_assert(NOCTI_V4_RTOS_PRIO_SCHED > NOCTI_V4_RTOS_PRIO_OPTICAL,
               "soft work stays below the acquisition ladder");
_Static_assert(NOCTI_V4_RTOS_PRIO_OPTICAL > NOCTI_V4_RTOS_PRIO_FRAME, "frame below optical");
_Static_assert(NOCTI_V4_RTOS_PRIO_FRAME > NOCTI_V4_RTOS_PRIO_NET, "UDP below the frame consumer");
_Static_assert(NOCTI_V4_RTOS_PRIO_NET > NOCTI_V4_RTOS_PRIO_CONTROL, "control below UDP");
_Static_assert(NOCTI_V4_RTOS_PRIO_CONTROL > NOCTI_V4_RTOS_PRIO_DARK, "dark diagnostic is lowest");
_Static_assert(NOCTI_V4_RTOS_BUDGET_ADS_SERVICE_US < NOCTI_V4_RTOS_BUDGET_DRDY_PERIOD_US,
               "ADS service budget must fit in one DRDY period");
