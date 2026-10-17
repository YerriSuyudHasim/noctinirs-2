#ifndef NN2_RTOS_CONFIG_H
#define NN2_RTOS_CONFIG_H

#include <stddef.h>
#include <stdint.h>

/* Single adapter header for task name, core, priority, and stack
   (ADR-0003 section 3.6). Values below are TBD(#35), proposed by
   Agent 05. They are not a Class H lock and they are not the
   historical 22/21/20 ladder.

   nn2_led and nn2_sched are not created in this slice. Their priority
   macros exist only so the ADS131 task can sit below both of them.
   nn2_ads1299 is not created here. It shares the ADS131 rank. */

#define NN2_TASK_NAME_ADS131 "nn2_ads131"
#define NN2_TASK_NAME_ADS1299 "nn2_ads1299"

#define NN2_RTOS_CORE_ADS131_TBD35 1

#define NN2_RTOS_PRIO_LED_TBD35 8
#define NN2_RTOS_PRIO_SCHED_TBD35 7
#define NN2_RTOS_PRIO_ADS131_TBD35 6
#define NN2_RTOS_PRIO_ADS1299_TBD35 6

/* 4096 bytes of stack. On this ESP-IDF v6.1 Xtensa port, StackType_t is
   uint8_t, so the depth argument and the buffer length are both the byte
   count. The adapter static-asserts that element size. Design margin is
   at least NN2_RTOS_STACK_MARGIN_MIN_BYTES. The high-water mark is Class H
   and is NOT RUN. */
#define NN2_RTOS_STACK_ADS131_BYTES 4096u
#define NN2_RTOS_STACK_MARGIN_MIN_BYTES 512u

/* Default ring depth until the data rate is fixed. The 50 ms backlog
   model is nn2_ads131_ring_model_depth(); it is not applied yet. */
#define NN2_ADS131_RING_DEPTH_DEFAULT 16u

_Static_assert(sizeof(NN2_TASK_NAME_ADS131) <= 16u, "nn2_ads131 name");
_Static_assert(sizeof(NN2_TASK_NAME_ADS1299) <= 16u, "nn2_ads1299 name");
_Static_assert(NN2_RTOS_CORE_ADS131_TBD35 == 1, "ADS131 core candidate");
_Static_assert(NN2_RTOS_PRIO_LED_TBD35 > NN2_RTOS_PRIO_SCHED_TBD35,
               "LED outranks the scheduler");
_Static_assert(NN2_RTOS_PRIO_SCHED_TBD35 > NN2_RTOS_PRIO_ADS131_TBD35,
               "scheduler outranks ADS131");
_Static_assert(NN2_RTOS_PRIO_ADS131_TBD35 == NN2_RTOS_PRIO_ADS1299_TBD35,
               "ADS131 and ADS1299 share a rank");
_Static_assert(NN2_RTOS_STACK_ADS131_BYTES >= NN2_RTOS_STACK_MARGIN_MIN_BYTES,
               "stack margin");
_Static_assert(NN2_RTOS_STACK_ADS131_BYTES == 4096u, "ADS131 stack is 4096 bytes");
_Static_assert(NN2_ADS131_RING_DEPTH_DEFAULT == 16u, "default ring depth");

#endif /* NN2_RTOS_CONFIG_H */
