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
    NN2_STATUS_COUNT_
} nn2_status_t;

/** One raw acquisition record. Values are raw counts; no physical/clinical meaning. */
typedef struct {
    uint32_t seq;          /**< monotonically increasing cycle number */
    uint8_t  channel;      /**< logical emitter/ADC channel index */
    uint32_t raw;          /**< raw ADC counts as delivered by the adc port */
    uint64_t t_led_on_us;  /**< timebase value when emitter was switched on */
    uint64_t t_sample_us;  /**< timebase value when sample was taken */
} nn2_core_sample_t;

/** Human-readable name for a status code (static string, never NULL). */
const char *nn2_core_status_str(nn2_status_t status);

#ifdef __cplusplus
}
#endif
#endif /* NN2_CORE_TYPES_H */
