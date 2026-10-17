#ifndef NN2_RTOS_CONFIG_H
#define NN2_RTOS_CONFIG_H

#include <stddef.h>
#include <stdint.h>

/* Single source for the M2 Wi-Fi task name, priority, stack, affinity, and
   event-queue depth (ADR-0003 section 3.6).

   These values are provisional for issue #30. ADR-0003 still leaves literal
   priorities, optical stack depth and unit, queue depths, and final affinity
   OPEN until Class H. This header does not freeze those cells and does not
   copy the historical 22/21/20 ladder.

   Stack macros are FreeRTOS words, which is the unit consumed by
   xTaskCreateStaticPinnedToCore. Optical tasks are not created here.

   Both tasks are pinned to core 0. This header defines no other core.
   The tasks are not watchdog subscribers yet (that mechanism is a later
   sequence). Their waits are still finite. */

#define NN2_TASK_NAME_NET "nn2_net"
#define NN2_TASK_NAME_PROV "nn2_prov"

#define NN2_RTOS_CORE_NET 0
#define NN2_RTOS_CORE_PROV 0

#define NN2_RTOS_PRIO_NET 3
#define NN2_RTOS_PRIO_PROV 2

#define NN2_RTOS_STACK_NET_WORDS 3072u
#define NN2_RTOS_STACK_PROV_WORDS 2560u

#define NN2_WIFI_EVT_QUEUE_LEN 8u

_Static_assert(sizeof(NN2_TASK_NAME_NET) <= 16u, "nn2_net name exceeds 15 characters");
_Static_assert(sizeof(NN2_TASK_NAME_PROV) <= 16u, "nn2_prov name exceeds 15 characters");
_Static_assert(NN2_RTOS_CORE_NET == 0, "station task must be core 0");
_Static_assert(NN2_RTOS_CORE_PROV == 0, "console task must be core 0");
_Static_assert(NN2_RTOS_PRIO_NET > NN2_RTOS_PRIO_PROV, "station task outranks console");
_Static_assert(NN2_WIFI_EVT_QUEUE_LEN >= 1u, "event queue depth");

#endif /* NN2_RTOS_CONFIG_H */
