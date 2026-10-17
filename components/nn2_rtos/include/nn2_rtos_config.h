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

   ESP-IDF xTaskCreateStaticPinnedToCore takes the stack depth in bytes,
   unlike vanilla FreeRTOS words (freertos/idf_additions.h). nn2_net is
   3072 B and nn2_prov is 2560 B. The static buffers are StackType_t arrays
   of that many elements: this ESP-IDF v6.1 kernel still multiplies the
   depth by sizeof(StackType_t) inside prvInitialiseNewTask, so a shorter
   buffer would be overrun. The nn2_net stack high-water mark is NOT RUN
   and is measured in Class H. Optical tasks are not created here.

   Both tasks are pinned to core 0. This header defines no other core.
   The Wi-Fi, lwIP TCP/IP, and esp_timer tasks are pinned to core 0 by
   sdkconfig. sys_evt has no affinity Kconfig in ESP-IDF v6.1:
   esp_event_loop_create_default sets task_core_id to 0
   (components/esp_event/default_event_loop.c). The tasks are not
   watchdog subscribers yet (that mechanism is a later sequence). Their
   waits are still finite. */

#define NN2_TASK_NAME_NET "nn2_net"
#define NN2_TASK_NAME_PROV "nn2_prov"

#define NN2_RTOS_CORE_NET 0
#define NN2_RTOS_CORE_PROV 0

#define NN2_RTOS_PRIO_NET 3
#define NN2_RTOS_PRIO_PROV 2

#define NN2_RTOS_STACK_NET_BYTES 3072u
#define NN2_RTOS_STACK_PROV_BYTES 2560u

#define NN2_WIFI_EVT_QUEUE_LEN 8u

_Static_assert(sizeof(NN2_TASK_NAME_NET) <= 16u, "nn2_net name exceeds 15 characters");
_Static_assert(sizeof(NN2_TASK_NAME_PROV) <= 16u, "nn2_prov name exceeds 15 characters");
_Static_assert(NN2_RTOS_CORE_NET == 0, "station task must be core 0");
_Static_assert(NN2_RTOS_CORE_PROV == 0, "console task must be core 0");
_Static_assert(NN2_RTOS_PRIO_NET > NN2_RTOS_PRIO_PROV, "station task outranks console");
_Static_assert(NN2_WIFI_EVT_QUEUE_LEN >= 1u, "event queue depth");
_Static_assert(NN2_RTOS_STACK_NET_BYTES == 3072u, "nn2_net stack is 3072 bytes");
_Static_assert(NN2_RTOS_STACK_PROV_BYTES == 2560u, "nn2_prov stack is 2560 bytes");

/* Pull Kconfig macros before the #error guards. Include order of other
   headers must not hide CONFIG_IDF_TARGET or the affinity symbols.
   Host Class U builds have no sdkconfig.h, so the include is skipped
   and the guards stay inactive. */
#if defined(__has_include)
#  if __has_include("sdkconfig.h")
#    include "sdkconfig.h"
#  endif
#endif
#if defined(CONFIG_IDF_TARGET)
#if !defined(CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0)
#error CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0 is required
#endif
#if !defined(CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0)
#error CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0 is required
#endif
#if !defined(CONFIG_ESP_TIMER_TASK_AFFINITY_CPU0)
#error CONFIG_ESP_TIMER_TASK_AFFINITY_CPU0 is required
#endif
#if (CONFIG_LWIP_TCPIP_TASK_AFFINITY != 0)
#error lwIP TCP/IP task must be pinned to core 0
#endif
#if (CONFIG_ESP_TIMER_TASK_AFFINITY != 0)
#error esp_timer task must be pinned to core 0
#endif
#endif

#endif /* NN2_RTOS_CONFIG_H */
