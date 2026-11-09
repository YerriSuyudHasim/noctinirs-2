#ifndef NOCTI_V4_STOP_DIAG_H
#define NOCTI_V4_STOP_DIAG_H

/* Batch 5A: stop and fault observability, variant A (16 B).
 *
 * Pure C policy with no ESP-IDF, FreeRTOS or driver dependency, so the same code
 * runs on the target and in the native host tests (ADR-0001: core policy does not
 * depend on a framework). The caller owns locking: every function here must run
 * under the caller's existing control lock. Nothing here allocates.
 *
 * Observation only. It never changes shutdown order, the value scan_stop() returns,
 * cleanup, or fault handling. It records:
 *   - the first control-layer failure (origin and error), and
 *   - the first stage of a STOP that made scan_stop() return non-OK, with its error,
 *   - the stages that failed in any attempt (cumulative mask), a per-session count of
 *     scan_stop() invocations, and whether the latest invocation returned OK.
 * Error codes of later failures and of the two drains are NOT retained.
 *
 * Coverage limit: these fields do not identify where the scheduler recorded its own
 * last_error (for example 259); that value can be set on paths that never reach
 * scan_stop(). */

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Control-failure origin. Values are stable: they are only printed by name. */
enum {
    NOCTI_V4_CTRL_ORIGIN_NONE = 0,
    NOCTI_V4_CTRL_ORIGIN_BOOT,
    NOCTI_V4_CTRL_ORIGIN_START,
    NOCTI_V4_CTRL_ORIGIN_RUNTIME_SCHED,
    NOCTI_V4_CTRL_ORIGIN_RUNTIME_ADS,
    NOCTI_V4_CTRL_ORIGIN_STOP
};

/* The four stages whose result can make scan_stop() return non-OK, in the order in
 * which scan_stop() executes them and in which it prefers their codes. */
enum {
    NOCTI_V4_STOP_STAGE_NONE = 0,
    NOCTI_V4_STOP_STAGE_SCHEDULER,
    NOCTI_V4_STOP_STAGE_ADS,
    NOCTI_V4_STOP_STAGE_OPTICAL,
    NOCTI_V4_STOP_STAGE_ASYNC_LED
};

/* stop_fail_mask bits. Bits 4 and 5 are observed-only: the baseline discards those
 * two results, so they can never make a STOP fail or populate failed_stop_stage. */
#define NOCTI_V4_STOP_BIT_SCHEDULER     0x01u
#define NOCTI_V4_STOP_BIT_ADS           0x02u
#define NOCTI_V4_STOP_BIT_OPTICAL       0x04u
#define NOCTI_V4_STOP_BIT_ASYNC_LED     0x08u
#define NOCTI_V4_STOP_BIT_FRAME_DRAIN   0x10u
#define NOCTI_V4_STOP_BIT_NETWORK_DRAIN 0x20u
#define NOCTI_V4_STOP_MASK_ALL          0x3Fu

/* Accumulated state. Field order is fixed so that sizeof is 16 B on 32-bit targets
 * and hosts: 4 + 4 + 5 byte-sized fields, padded to the 4-byte alignment. */
typedef struct {
    int32_t ctrl_error;        /* error of the first control failure; 0 when none */
    int32_t stop_stage_error;  /* error of failed_stop_stage; 0 when none */
    uint8_t ctrl_origin;       /* NOCTI_V4_CTRL_ORIGIN_* */
    uint8_t failed_stop_stage; /* NOCTI_V4_STOP_STAGE_* */
    uint8_t stop_fail_mask;    /* cumulative since the last reset */
    uint8_t stop_attempts;     /* scan_stop() invocations since reset, saturates at 255 */
    uint8_t stop_last_ok;      /* 1 when the latest scan_stop() returned OK; valid if attempts > 0 */
} nocti_v4_stop_diag_t;

_Static_assert(sizeof(nocti_v4_stop_diag_t) == 16u,
               "Batch 5A diagnostic record is 16 B; changing it needs a budget decision");

/* One scan_stop() invocation, derived from the results scan_stop() already holds. */
typedef struct {
    int32_t error;  /* the code scan_stop() returns; 0 when it returns OK */
    uint8_t stage;  /* first non-OK of the four return-affecting stages, else NONE */
    uint8_t mask;   /* every stage that returned non-OK, including the two drains */
} nocti_v4_stop_report_t;

/* Baseline precedence (app_main.c scan_stop): scheduler, then ADS, then optical, then
 * async LED. The two drain arguments only set mask bits. Error codes are int32_t
 * (esp_err_t); 0 is OK. */
static inline void nocti_v4_stop_report_make(nocti_v4_stop_report_t *report,
                                             int32_t scheduler, int32_t ads,
                                             int32_t optical, int32_t async_led,
                                             int32_t frame_drain, int32_t network_drain)
{
    uint8_t mask = 0u;
    if (scheduler != 0) mask |= NOCTI_V4_STOP_BIT_SCHEDULER;
    if (ads != 0) mask |= NOCTI_V4_STOP_BIT_ADS;
    if (optical != 0) mask |= NOCTI_V4_STOP_BIT_OPTICAL;
    if (async_led != 0) mask |= NOCTI_V4_STOP_BIT_ASYNC_LED;
    if (frame_drain != 0) mask |= NOCTI_V4_STOP_BIT_FRAME_DRAIN;
    if (network_drain != 0) mask |= NOCTI_V4_STOP_BIT_NETWORK_DRAIN;
    report->mask = mask;
    if (scheduler != 0) {
        report->stage = NOCTI_V4_STOP_STAGE_SCHEDULER; report->error = scheduler;
    } else if (ads != 0) {
        report->stage = NOCTI_V4_STOP_STAGE_ADS; report->error = ads;
    } else if (optical != 0) {
        report->stage = NOCTI_V4_STOP_STAGE_OPTICAL; report->error = optical;
    } else if (async_led != 0) {
        report->stage = NOCTI_V4_STOP_STAGE_ASYNC_LED; report->error = async_led;
    } else {
        report->stage = NOCTI_V4_STOP_STAGE_NONE; report->error = 0;
    }
}

/* Boot and accepted START. A latched failure is never erased by this in practice:
 * every failure site also sets the sticky control fault, which refuses START. */
static inline void nocti_v4_stop_diag_reset(nocti_v4_stop_diag_t *diag)
{
    diag->ctrl_error = 0;
    diag->stop_stage_error = 0;
    diag->ctrl_origin = NOCTI_V4_CTRL_ORIGIN_NONE;
    diag->failed_stop_stage = NOCTI_V4_STOP_STAGE_NONE;
    diag->stop_fail_mask = 0u;
    diag->stop_attempts = 0u;
    diag->stop_last_ok = 0u;
}

/* First control-layer failure wins. Later calls never overwrite it. */
static inline void nocti_v4_stop_diag_note_ctrl(nocti_v4_stop_diag_t *diag,
                                                uint8_t origin, int32_t error)
{
    if (diag->ctrl_origin == NOCTI_V4_CTRL_ORIGIN_NONE &&
        origin != NOCTI_V4_CTRL_ORIGIN_NONE) {
        diag->ctrl_origin = origin;
        diag->ctrl_error = error;
    }
}

/* One call per scan_stop() invocation, including repeated cleanup attempts. */
static inline void nocti_v4_stop_diag_commit(nocti_v4_stop_diag_t *diag,
                                             const nocti_v4_stop_report_t *report)
{
    if (diag->stop_attempts < UINT8_MAX) ++diag->stop_attempts;
    diag->stop_fail_mask = (uint8_t)(diag->stop_fail_mask |
                                     (report->mask & NOCTI_V4_STOP_MASK_ALL));
    diag->stop_last_ok = report->stage == NOCTI_V4_STOP_STAGE_NONE ? 1u : 0u;
    if (report->stage != NOCTI_V4_STOP_STAGE_NONE) {
        if (diag->failed_stop_stage == NOCTI_V4_STOP_STAGE_NONE) {
            diag->failed_stop_stage = report->stage;
            diag->stop_stage_error = report->error;
        }
        nocti_v4_stop_diag_note_ctrl(diag, NOCTI_V4_CTRL_ORIGIN_STOP, report->error);
    }
}

static inline const char *nocti_v4_ctrl_origin_name(uint8_t origin)
{
    switch (origin) {
    case NOCTI_V4_CTRL_ORIGIN_NONE: return "NONE";
    case NOCTI_V4_CTRL_ORIGIN_BOOT: return "BOOT";
    case NOCTI_V4_CTRL_ORIGIN_START: return "START";
    case NOCTI_V4_CTRL_ORIGIN_RUNTIME_SCHED: return "RUNTIME_SCHED";
    case NOCTI_V4_CTRL_ORIGIN_RUNTIME_ADS: return "RUNTIME_ADS";
    case NOCTI_V4_CTRL_ORIGIN_STOP: return "STOP";
    default: return "INVALID";
    }
}

static inline const char *nocti_v4_stop_stage_name(uint8_t stage)
{
    switch (stage) {
    case NOCTI_V4_STOP_STAGE_NONE: return "NONE";
    case NOCTI_V4_STOP_STAGE_SCHEDULER: return "SCHEDULER";
    case NOCTI_V4_STOP_STAGE_ADS: return "ADS";
    case NOCTI_V4_STOP_STAGE_OPTICAL: return "OPTICAL";
    case NOCTI_V4_STOP_STAGE_ASYNC_LED: return "ASYNC_LED";
    default: return "INVALID";
    }
}

/* Appends the eight get_netdiag keys (leading space included). Writes a complete
 * text or nothing: on overflow it leaves out[0] = 0 and returns 0, so the caller's
 * earlier text stays intact. Returns the number of characters appended.
 *
 * stop_other_codes is "none" only when the mask holds no bit other than the one of
 * failed_stop_stage; otherwise "unavailable" (codes of additional failures and of the
 * two drains are not retained). */
static inline size_t nocti_v4_stop_diag_format(const nocti_v4_stop_diag_t *diag,
                                               char *out, size_t cap)
{
    if (out == NULL || cap == 0u) return 0u;
    uint8_t own = 0u;
    switch (diag->failed_stop_stage) {
    case NOCTI_V4_STOP_STAGE_SCHEDULER: own = NOCTI_V4_STOP_BIT_SCHEDULER; break;
    case NOCTI_V4_STOP_STAGE_ADS: own = NOCTI_V4_STOP_BIT_ADS; break;
    case NOCTI_V4_STOP_STAGE_OPTICAL: own = NOCTI_V4_STOP_BIT_OPTICAL; break;
    case NOCTI_V4_STOP_STAGE_ASYNC_LED: own = NOCTI_V4_STOP_BIT_ASYNC_LED; break;
    default: break;
    }
    const char *last_ok = diag->stop_attempts == 0u ? "na" : (diag->stop_last_ok ? "1" : "0");
    const char *others = (diag->stop_fail_mask & (uint8_t)~own) == 0u ? "none" : "unavailable";
    const int n = snprintf(out, cap,
        " ctrl_origin=%s ctrl_error=%" PRId32 " failed_stop_stage=%s stop_stage_error=%" PRId32
        " stop_fail_mask=0x%02X stop_attempts=%u stop_last_ok=%s stop_other_codes=%s",
        nocti_v4_ctrl_origin_name(diag->ctrl_origin), diag->ctrl_error,
        nocti_v4_stop_stage_name(diag->failed_stop_stage), diag->stop_stage_error,
        (unsigned)diag->stop_fail_mask, (unsigned)diag->stop_attempts, last_ok, others);
    if (n < 0 || (size_t)n >= cap) { out[0] = '\0'; return 0u; }
    return (size_t)n;
}

#endif
