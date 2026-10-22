/* Composition-root helpers for issue #49. Reads the reset reason before any
 * later boot step, and reports whether a SPIRAM heap region exists.
 * Does not create a task, queue, timer, or Wi-Fi client, and does not touch
 * the task watchdog. */
#ifndef NN2_BOOT_ESP_H
#define NN2_BOOT_ESP_H

#include "nn2_app_safe_boot.h"

nn2_reset_reason_t nn2_adapter_read_reset_reason(void);
uint32_t nn2_adapter_spiram_bytes(void);

#endif
