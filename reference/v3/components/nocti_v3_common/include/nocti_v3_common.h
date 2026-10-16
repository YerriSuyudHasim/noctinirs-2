#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "nocti_v3_config.h"
#include "nocti_v3_version.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NOCTI_V3_STATE_STOPPED = 0,
    NOCTI_V3_STATE_STARTING_DARK,
    NOCTI_V3_STATE_RUNNING_DARK,
    NOCTI_V3_STATE_STARTING_SCAN,
    NOCTI_V3_STATE_RUNNING_SCAN,
    NOCTI_V3_STATE_STOPPING,
    NOCTI_V3_STATE_RECOVERING,
    NOCTI_V3_STATE_FAULTED,
} nocti_v3_lifecycle_state_t;

typedef enum {
    NOCTI_V3_MODE_STOPPED = 0,
    NOCTI_V3_MODE_DARK,
    NOCTI_V3_MODE_SINGLE,
    NOCTI_V3_MODE_SCAN,
} nocti_v3_mode_t;

typedef enum {
    NOCTI_V3_FAULT_NONE = 0,
    NOCTI_V3_FAULT_CONFIG,
    NOCTI_V3_FAULT_TIMEBASE,
    NOCTI_V3_FAULT_I2C_INIT,
    NOCTI_V3_FAULT_LED_UNSAFE,
    NOCTI_V3_FAULT_WIFI_INIT,
    NOCTI_V3_FAULT_NOT_IMPLEMENTED,
    NOCTI_V3_FAULT_ADC_INIT,
    NOCTI_V3_FAULT_ADC_RATE,
    NOCTI_V3_FAULT_ADC_INTEGRITY,
    NOCTI_V3_FAULT_SPI,
    NOCTI_V3_FAULT_DRDY_EVENT_OVERFLOW,
    NOCTI_V3_FAULT_ADC_RING_OVERFLOW,
    NOCTI_V3_FAULT_LED_I2C,
    NOCTI_V3_FAULT_LED_TIMEOUT,
    NOCTI_V3_FAULT_LED_RECOVERY,
    NOCTI_V3_FAULT_SCHEDULER,
    NOCTI_V3_FAULT_SCHEDULER_EVENT_OVERFLOW,
    NOCTI_V3_FAULT_ADC_DRDY_STOPPED,
    NOCTI_V3_FAULT_ADC_REPEATED_CRC,
    NOCTI_V3_FAULT_ADC_REPEATED_STATUS,
    NOCTI_V3_FAULT_OPTICAL_PROCESSING,
    NOCTI_V3_FAULT_COMPLETED_FRAME_RING,
    NOCTI_V3_FAULT_STALE_CALLBACK,
} nocti_v3_fault_code_t;

const char *nocti_v3_state_name(nocti_v3_lifecycle_state_t state);
const char *nocti_v3_mode_name(nocti_v3_mode_t mode);
const char *nocti_v3_fault_name(nocti_v3_fault_code_t fault);

#ifdef __cplusplus
}
#endif
