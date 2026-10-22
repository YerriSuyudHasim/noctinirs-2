/* Boot safe-off policy (issue #49). IDF-free. No tasks, queues, or delays of
 * its own: every hardware action goes through nn2_port_emitter_safe_t.
 *
 * Order on every boot path that passes the SPIRAM check:
 *   record reset reason, RESET# asserted, release RESET#, wait 2 ms,
 *   IREFALL=0, PWMALL=0, LEDOUT0-5=0, read IREF0-23 and LEDOUT0-5,
 *   RESET# asserted and held.
 * The 2 ms wait is the issue's suggested value and meets >= 1.5 ms.
 * One bus attempt. No retry loop. MODE1/SLEEP is not requested.
 * Readback or bus failure leaves RESET# asserted and outcome
 * "FAULTED (BOOT)". ADR-0004 is not in this repository; this outcome is the
 * issue #49 name, not a full fault-taxonomy implementation.
 * A non-zero spiram_bytes value never releases RESET#.
 * emitter_enabled stays false: this policy has no LED-drive request.
 *
 * Protocol get_status/get_info does not exist yet. reset_reason in the result
 * is the value those commands must expose once a protocol core exists.
 *
 * Caller context: one caller in app_main, never an ISR. Callbacks run
 * synchronously in that caller. Cleanup (RESET# asserted) runs in that caller.
 */
#ifndef NN2_APP_SAFE_BOOT_H
#define NN2_APP_SAFE_BOOT_H

#include "nn2_port_emitter_safe.h"

#define NN2_APP_SAFE_BOOT_RELEASE_MS 2u
#define NN2_APP_SAFE_BOOT_RELEASE_MIN_TENTHS_MS 15u
#define NN2_APP_SAFE_BOOT_ATTEMPT_LIMIT 1u

/* Portable snapshot of esp_reset_reason_t ordinals from ESP-IDF v6.1
 * esp_system.h. The composition-root adapter static-asserts the match.
 * Numeric order is the IDF enum order, not a hardware measurement. */
typedef enum {
    NN2_RESET_UNKNOWN = 0,
    NN2_RESET_POWERON,
    NN2_RESET_EXT,
    NN2_RESET_SW,
    NN2_RESET_PANIC,
    NN2_RESET_INT_WDT,
    NN2_RESET_TASK_WDT,
    NN2_RESET_WDT,
    NN2_RESET_DEEPSLEEP,
    NN2_RESET_BROWNOUT,
    NN2_RESET_SDIO,
    NN2_RESET_USB,
    NN2_RESET_JTAG,
    NN2_RESET_EFUSE,
    NN2_RESET_PWR_GLITCH,
    NN2_RESET_CPU_LOCKUP,
    NN2_RESET_COUNT_
} nn2_reset_reason_t;

typedef enum {
    NN2_BOOT_OK = 0,
    NN2_BOOT_FAULTED,
    NN2_BOOT_FAULTED_SPIRAM
} nn2_boot_outcome_t;

typedef struct {
    nn2_reset_reason_t reset_reason;
    uint32_t spiram_bytes;
} nn2_app_safe_boot_input_t;

typedef struct {
    nn2_boot_outcome_t outcome;
    nn2_status_t error;
    nn2_reset_reason_t reset_reason;
    uint32_t attempts;
    bool reset_held_low;
    bool emitter_enabled;
} nn2_app_safe_boot_result_t;

typedef void (*nn2_app_safe_boot_error_fn)(void *ctx, nn2_boot_outcome_t outcome,
    nn2_status_t error);

/* on_error is required. It is called once on every failure, after RESET# has
 * been requested asserted, and is not called on success. */
nn2_status_t nn2_app_safe_boot_run(const nn2_app_safe_boot_input_t *in,
    const nn2_port_emitter_safe_t *port, nn2_app_safe_boot_error_fn on_error,
    void *log_ctx, nn2_app_safe_boot_result_t *out);

const char *nn2_app_reset_reason_str(nn2_reset_reason_t reason);
const char *nn2_app_safe_boot_outcome_str(nn2_boot_outcome_t outcome);

#endif /* NN2_APP_SAFE_BOOT_H */
