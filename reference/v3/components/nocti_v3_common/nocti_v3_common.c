#include "nocti_v3_common.h"

static const nocti_v3_timing_config_t s_timing = {
    .frame_rate_hz = NOCTI_V3_FRAME_RATE_HZ,
    .frame_period_us = NOCTI_V3_FRAME_PERIOD_US,
    .slot_count = NOCTI_V3_SLOT_COUNT,
    .slot_period_us = NOCTI_V3_SLOT_PERIOD_US,
    .led_off_offset_us = NOCTI_V3_LED_OFF_OFFSET_US,
    .on_budget_us = NOCTI_V3_ON_BUDGET_US,
    .off_budget_us = NOCTI_V3_OFF_BUDGET_US,
    .settle_on_us = NOCTI_V3_SETTLE_ON_US,
    .settle_off_us = NOCTI_V3_SETTLE_OFF_US,
    .target_window_on_us = NOCTI_V3_TARGET_WINDOW_ON_US,
    .target_window_off_us = NOCTI_V3_TARGET_WINDOW_OFF_US,
    .min_window_on_us = NOCTI_V3_MIN_WINDOW_ON_US,
    .min_window_off_us = NOCTI_V3_MIN_WINDOW_OFF_US,
    .end_guard_us = NOCTI_V3_END_GUARD_US,
    .i2c_commit_allowance_us = NOCTI_V3_I2C_COMMIT_ALLOWANCE_US,
    .adc_sample_rate_hz = NOCTI_V3_ADS_SAMPLE_RATE_HZ,
    .adc_period_us = NOCTI_V3_ADS_PERIOD_US,
    .ads_spi_clock_hz = NOCTI_V3_ADS_SPI_CLOCK_HZ,
    .i2c_clock_hz = NOCTI_V3_I2C_CLOCK_SELECTED_HZ,
    .k_target_on = NOCTI_V3_K_TARGET_ON,
    .k_target_off = NOCTI_V3_K_TARGET_OFF,
    .k_min_on = NOCTI_V3_K_MIN_ON,
    .k_min_off = NOCTI_V3_K_MIN_OFF,
};

static const nocti_v3_task_plan_t s_tasks = {
    .adc_core = NOCTI_V3_CORE_REALTIME,
    .scheduler_core = NOCTI_V3_CORE_REALTIME,
    .led_control_core = NOCTI_V3_CORE_REALTIME,
    .optical_core = NOCTI_V3_CORE_NETWORK,
    .led_recovery_core = NOCTI_V3_CORE_REALTIME,
    .frame_sink_core = NOCTI_V3_CORE_NETWORK,
    .network_core = NOCTI_V3_CORE_NETWORK,
    .udp_stream_core = NOCTI_V3_CORE_NETWORK,
    .health_core = NOCTI_V3_CORE_NETWORK,
    .adc_priority = NOCTI_V3_PRIO_ADC,
    .scheduler_priority = NOCTI_V3_PRIO_SCHEDULER,
    .led_control_priority = NOCTI_V3_PRIO_LED_CONTROL,
    .optical_priority = NOCTI_V3_PRIO_OPTICAL_PROCESS,
    .led_recovery_priority = NOCTI_V3_PRIO_LED_RECOVERY,
    .frame_sink_priority = NOCTI_V3_PRIO_FRAME_SINK,
    .udp_control_priority = NOCTI_V3_PRIO_UDP_CONTROL,
    .udp_stream_priority = NOCTI_V3_PRIO_UDP_TX,
    .health_priority = NOCTI_V3_PRIO_HEALTH,
    .adc_stack_bytes = NOCTI_V3_STACK_ADC_BYTES,
    .scheduler_stack_bytes = NOCTI_V3_STACK_SCHEDULER_BYTES,
    .led_control_stack_bytes = NOCTI_V3_STACK_LED_CONTROL_BYTES,
    .optical_stack_bytes = NOCTI_V3_STACK_OPTICAL_BYTES,
    .led_recovery_stack_bytes = NOCTI_V3_STACK_LED_RECOVERY_BYTES,
    .frame_sink_stack_bytes = NOCTI_V3_STACK_FRAME_SINK_BYTES,
    .udp_control_stack_bytes = NOCTI_V3_STACK_UDP_CONTROL_BYTES,
    .udp_stream_stack_bytes = NOCTI_V3_STACK_UDP_STREAM_BYTES,
    .health_stack_bytes = NOCTI_V3_STACK_HEALTH_BYTES,
};

const nocti_v3_timing_config_t *nocti_v3_timing_config(void)
{
    return &s_timing;
}

const nocti_v3_task_plan_t *nocti_v3_task_plan(void)
{
    return &s_tasks;
}

bool nocti_v3_timing_runtime_validate(void)
{
    const nocti_v3_timing_config_t *t = &s_timing;
    if (t->slot_count * t->slot_period_us != t->frame_period_us) return false;
    if (t->frame_rate_hz * t->frame_period_us != 1000000u) return false;
    if (t->on_budget_us + t->off_budget_us != t->slot_period_us) return false;
    if (t->led_off_offset_us >= t->slot_period_us) return false;
    if (t->k_target_on < t->k_min_on || t->k_target_off < t->k_min_off) return false;
    if (t->i2c_commit_allowance_us + t->settle_on_us +
            t->target_window_on_us > t->on_budget_us) return false;
    if (t->i2c_commit_allowance_us + t->settle_off_us +
            t->target_window_off_us + t->end_guard_us > t->off_budget_us) {
        return false;
    }
    return true;
}

const char *nocti_v3_state_name(nocti_v3_lifecycle_state_t state)
{
    switch (state) {
        case NOCTI_V3_STATE_STOPPED: return "STOPPED";
        case NOCTI_V3_STATE_STARTING_DARK: return "STARTING_DARK";
        case NOCTI_V3_STATE_RUNNING_DARK: return "RUNNING_DARK";
        case NOCTI_V3_STATE_STARTING_SCAN: return "STARTING_SCAN";
        case NOCTI_V3_STATE_RUNNING_SCAN: return "RUNNING_SCAN";
        case NOCTI_V3_STATE_STOPPING: return "STOPPING";
        case NOCTI_V3_STATE_RECOVERING: return "RECOVERING";
        case NOCTI_V3_STATE_FAULTED: return "FAULTED";
        default: return "UNKNOWN";
    }
}

const char *nocti_v3_mode_name(nocti_v3_mode_t mode)
{
    switch (mode) {
        case NOCTI_V3_MODE_STOPPED: return "STOPPED";
        case NOCTI_V3_MODE_DARK: return "DARK";
        case NOCTI_V3_MODE_SINGLE: return "SINGLE";
        case NOCTI_V3_MODE_SCAN: return "SCAN";
        default: return "UNKNOWN";
    }
}

const char *nocti_v3_fault_name(nocti_v3_fault_code_t fault)
{
    switch (fault) {
        case NOCTI_V3_FAULT_NONE: return "NONE";
        case NOCTI_V3_FAULT_CONFIG: return "CONFIG";
        case NOCTI_V3_FAULT_TIMEBASE: return "TIMEBASE";
        case NOCTI_V3_FAULT_I2C_INIT: return "I2C_INIT";
        case NOCTI_V3_FAULT_LED_UNSAFE: return "LED_UNSAFE";
        case NOCTI_V3_FAULT_WIFI_INIT: return "WIFI_INIT";
        case NOCTI_V3_FAULT_NOT_IMPLEMENTED: return "NOT_IMPLEMENTED";
        case NOCTI_V3_FAULT_ADC_INIT: return "ADC_INIT";
        case NOCTI_V3_FAULT_ADC_RATE: return "ADC_RATE";
        case NOCTI_V3_FAULT_ADC_INTEGRITY: return "ADC_INTEGRITY";
        case NOCTI_V3_FAULT_SPI: return "SPI";
        case NOCTI_V3_FAULT_DRDY_EVENT_OVERFLOW: return "DRDY_EVENT_OVERFLOW";
        case NOCTI_V3_FAULT_ADC_RING_OVERFLOW: return "ADC_RING_OVERFLOW";
        case NOCTI_V3_FAULT_LED_I2C: return "LED_I2C";
        case NOCTI_V3_FAULT_LED_TIMEOUT: return "LED_TIMEOUT";
        case NOCTI_V3_FAULT_LED_RECOVERY: return "LED_RECOVERY";
        case NOCTI_V3_FAULT_SCHEDULER: return "SCHEDULER";
        case NOCTI_V3_FAULT_SCHEDULER_EVENT_OVERFLOW: return "SCHEDULER_EVENT_OVERFLOW";
        case NOCTI_V3_FAULT_ADC_DRDY_STOPPED: return "ADC_DRDY_STOPPED";
        case NOCTI_V3_FAULT_ADC_REPEATED_CRC: return "ADC_REPEATED_CRC";
        case NOCTI_V3_FAULT_ADC_REPEATED_STATUS: return "ADC_REPEATED_STATUS";
        case NOCTI_V3_FAULT_OPTICAL_PROCESSING: return "OPTICAL_PROCESSING";
        case NOCTI_V3_FAULT_COMPLETED_FRAME_RING: return "COMPLETED_FRAME_RING";
        case NOCTI_V3_FAULT_STALE_CALLBACK: return "STALE_CALLBACK";
        default: return "UNKNOWN";
    }
}
