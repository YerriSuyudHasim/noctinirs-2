/* Concrete API contract: docs/ISSUE13_COMPONENT_CONTRACT.md.
 * Caller-owned typed storage, exclusive task context, no ISR/reentry.
 * Value/port contracts are separate from opaque operational module handles. */
/* NoctiNIRS 2 Core types (ADR-0001 Core layer). Pure C11, no platform headers. */
#ifndef NN2_CORE_TYPES_H
#define NN2_CORE_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NN2_VERSION_MAJOR 0
#define NN2_VERSION_MINOR 1
#define NN2_VERSION_PATCH 0
#define NN2_VERSION_STRING "0.1.0"

/** Status codes. NN2_OK == 0. Adapters translate platform errors to these. */
typedef enum {
    NN2_OK = 0,
    NN2_ERR_INVALID_ARG,
    NN2_ERR_INVALID_STATE,
    NN2_ERR_FULL,
    NN2_ERR_EMPTY,
    NN2_ERR_IO,
    NN2_ERR_NOT_SUPPORTED,
    NN2_ERR_TIMEOUT,
    /* Issue #30 provision codes. Appended so the values above stay stable. */
    NN2_ERR_MISSING,
    NN2_ERR_INVALID,
    NN2_ERR_NVS_CORRUPT,
    NN2_ERR_AUTH_FAIL,
    NN2_ERR_CANCELLED,
    NN2_ERR_NO_MEM,
    NN2_ERR_BUSY,
    NN2_ERR_OVERFLOW,
    NN2_STATUS_COUNT_
} nn2_status_t;

/** Provenance of a sample value. Zero-initialised records read UNKNOWN.
 *  FAKE = synthetic value from a disabled/fake adapter, NOT a measurement.
 *  ADC  = value from a real ADC adapter (none exists in 0.1.x). */
typedef enum {
    NN2_SAMPLE_SRC_UNKNOWN = 0,
    NN2_SAMPLE_SRC_FAKE,
    NN2_SAMPLE_SRC_ADC
} nn2_sample_src_t;

/** One raw acquisition record. Values are raw counts; no physical/clinical meaning. */
typedef struct {
    uint32_t seq;          /**< cycle number, wraps modulo 2^32 */
    uint8_t  channel;      /**< logical emitter/ADC channel index */
    uint32_t raw;          /**< raw ADC counts as delivered by the adc port */
    uint64_t t_led_on_us;  /**< timebase after on() returned OK, not a physical edge */
    uint64_t t_sample_us;  /**< timebase after read() returned OK, not conversion edge */
    nn2_sample_src_t src;  /**< provenance of raw; copied from the adc port */
} nn2_core_sample_t;

/** Human-readable name for a status code (static string, never NULL). */
const char *nn2_core_status_str(nn2_status_t status);
/** Name for a sample provenance value (static string, never NULL). */
const char *nn2_core_sample_src_str(nn2_sample_src_t src);

#ifdef __cplusplus
}
#endif
#endif /* NN2_CORE_TYPES_H */
