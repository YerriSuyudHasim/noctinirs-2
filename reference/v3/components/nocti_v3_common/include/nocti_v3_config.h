#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sdkconfig.h"

/* This firmware intentionally retains the V2 ADS131 GPIO35-38 mapping.
 * Do not permit an accidental software PSRAM enable in this board profile. */
#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || \
    (defined(CONFIG_ESP32S3_SPIRAM_SUPPORT) && CONFIG_ESP32S3_SPIRAM_SUPPORT)
#error "NoctiNIRSv3 Noctic_v1 profile requires PSRAM disabled"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Single source of truth for NoctiNIRSv3 timing and task planning. */
#define NOCTI_V3_FRAME_RATE_HZ                 10u
#define NOCTI_V3_SLOT_COUNT                    20u
#define NOCTI_V3_FRAME_PERIOD_US               100000u
#define NOCTI_V3_SLOT_PERIOD_US                5000u
#define NOCTI_V3_LED_OFF_OFFSET_US             2500u
#define NOCTI_V3_ON_BUDGET_US                  NOCTI_V3_LED_OFF_OFFSET_US
#define NOCTI_V3_OFF_BUDGET_US                 \
    (NOCTI_V3_SLOT_PERIOD_US - NOCTI_V3_LED_OFF_OFFSET_US)
#define NOCTI_V3_SETTLE_ON_US                  500u
#define NOCTI_V3_SETTLE_OFF_US                 500u
#define NOCTI_V3_TARGET_WINDOW_ON_US           1250u
#define NOCTI_V3_TARGET_WINDOW_OFF_US          1250u
#define NOCTI_V3_MIN_WINDOW_ON_US              1000u
#define NOCTI_V3_MIN_WINDOW_OFF_US             1000u
#define NOCTI_V3_END_GUARD_US                  400u

#define NOCTI_V3_ADS_SAMPLE_RATE_HZ            8000u
#define NOCTI_V3_ADS_PERIOD_US                 125u
#define NOCTI_V3_ADS_SPI_CLOCK_HZ              8000000u
#define NOCTI_V3_ADS_SPI_MODE                  1u
#define NOCTI_V3_ADS_WORD_BITS                 24u
#define NOCTI_V3_K_TARGET_ON                   10u
#define NOCTI_V3_K_TARGET_OFF                  10u
#define NOCTI_V3_K_MIN_ON                      8u
#define NOCTI_V3_K_MIN_OFF                     8u

#define NOCTI_V3_I2C_CLOCK_DEFAULT_HZ          400000u
#define NOCTI_V3_I2C_CLOCK_EXPERIMENT_HZ       1000000u
#if defined(CONFIG_NOCTI_V3_I2C_PROFILE_1MHZ_EXPERIMENT) && \
    CONFIG_NOCTI_V3_I2C_PROFILE_1MHZ_EXPERIMENT
#define NOCTI_V3_I2C_CLOCK_SELECTED_HZ          NOCTI_V3_I2C_CLOCK_EXPERIMENT_HZ
#define NOCTI_V3_I2C_PROFILE_NAME               "1MHZ_EXPERIMENT"
#else
#define NOCTI_V3_I2C_CLOCK_SELECTED_HZ          NOCTI_V3_I2C_CLOCK_DEFAULT_HZ
#define NOCTI_V3_I2C_PROFILE_NAME               "400KHZ_DEFAULT"
#endif
#define NOCTI_V3_I2C_SCL_WAIT_US               1000u
#define NOCTI_V3_I2C_ATOMIC_LEDOUT_BITS        72u
/* Conservative design allowance; actual p99/max must be measured on hardware. */
#define NOCTI_V3_I2C_COMMIT_ALLOWANCE_US       250u

#define NOCTI_V3_UDP_CTRL_PORT                 3333u
#define NOCTI_V3_UDP_STREAM_PORT               3334u
#define NOCTI_V3_WIFI_AP_CHANNEL               6u
#define NOCTI_V3_WIFI_AP_MAX_CONNECTIONS       2u
#define NOCTI_V3_WIFI_AP_SSID                  "NoctiNIRS"
#define NOCTI_V3_WIFI_AP_PASSWORD              "noctinirs123"

/* Application task layout for the final Prompt 5 architecture. */
#define NOCTI_V3_CORE_NETWORK                  0
#define NOCTI_V3_CORE_REALTIME                 1
#define NOCTI_V3_PRIO_ADC                      20u
#define NOCTI_V3_PRIO_SCHEDULER                19u
#define NOCTI_V3_PRIO_LED_CONTROL              18u
#define NOCTI_V3_PRIO_OPTICAL_PROCESS          12u
#define NOCTI_V3_PRIO_LED_RECOVERY             10u
#define NOCTI_V3_PRIO_UDP_CONTROL              9u
#define NOCTI_V3_PRIO_UDP_TX                   8u
#define NOCTI_V3_PRIO_HEALTH                   5u
#define NOCTI_V3_STACK_ADC_BYTES               4096u
#define NOCTI_V3_STACK_SCHEDULER_BYTES         4096u
#define NOCTI_V3_STACK_LED_CONTROL_BYTES       5120u
#define NOCTI_V3_STACK_LED_RECOVERY_BYTES      5120u
#define NOCTI_V3_STACK_OPTICAL_BYTES           6144u
#define NOCTI_V3_STACK_UDP_CONTROL_BYTES       6144u
#define NOCTI_V3_STACK_HEALTH_BYTES            4096u

/* Compile-time enforcement of the intended dependency-aware priority order. */
_Static_assert(NOCTI_V3_PRIO_ADC > NOCTI_V3_PRIO_SCHEDULER,
               "ADC acquisition must outrank scheduler task work");
_Static_assert(NOCTI_V3_PRIO_SCHEDULER > NOCTI_V3_PRIO_LED_CONTROL,
               "scheduler must outrank LED-control submission");
_Static_assert(NOCTI_V3_PRIO_LED_CONTROL > NOCTI_V3_PRIO_OPTICAL_PROCESS,
               "LED-control must outrank optical processing");
_Static_assert(NOCTI_V3_PRIO_OPTICAL_PROCESS > NOCTI_V3_PRIO_LED_RECOVERY,
               "normal processing must outrank fault-only recovery");
_Static_assert(NOCTI_V3_PRIO_LED_RECOVERY > NOCTI_V3_PRIO_UDP_CONTROL,
               "LED recovery must outrank non-real-time commands");
_Static_assert(NOCTI_V3_PRIO_UDP_CONTROL > NOCTI_V3_PRIO_UDP_TX,
               "command handling must not outrank the real-time path");
_Static_assert(NOCTI_V3_PRIO_UDP_TX > NOCTI_V3_PRIO_HEALTH,
               "health logger must remain the lowest application priority");

/* Prompt 5 decoupled UDP stream and transport policy. */
#define NOCTI_V3_STACK_UDP_STREAM_BYTES        6144u
#define NOCTI_V3_UDP_STREAM_WAIT_MS            20u
#define NOCTI_V3_UDP_STREAM_RESET_TIMEOUT_MS   500u
#define NOCTI_V3_NETWORK_TASK_STOP_TIMEOUT_MS   1000u
#define NOCTI_V3_STREAM_TIME64_PERIOD_US       30000000u
#define NOCTI_V3_LED_CURRENT_MAX_MA            50u

/* Prompt 3 PCA9956B asynchronous LED path and optical scheduler. */
#define NOCTI_V3_PCA_RESET_LOW_US              5u
#define NOCTI_V3_PCA_RESET_RECOVERY_US         1600u
#define NOCTI_V3_PCA_REXT_OHM                  1000u
#define NOCTI_V3_LED_CURRENT_735_MA             15u
#define NOCTI_V3_LED_CURRENT_850_MA             15u
#define NOCTI_V3_I2C_ASYNC_QUEUE_DEPTH          4u
#define NOCTI_V3_I2C_RUNTIME_TIMEOUT_MS         2u
#define NOCTI_V3_I2C_INIT_TIMEOUT_MS            50u
#define NOCTI_V3_LED_REQUEST_QUEUE_CAPACITY     8u
#define NOCTI_V3_LED_COMPLETION_RING_CAPACITY   16u
#define NOCTI_V3_LED_COMMAND_TIMEOUT_US         2000u
#define NOCTI_V3_LED_CONSECUTIVE_ERROR_THRESHOLD 3u
#define NOCTI_V3_LED_RECOVERY_COOLDOWN_MS       20u
#define NOCTI_V3_SCHEDULER_CONTROL_TIMEOUT_MS   5000u
#define NOCTI_V3_SCAN_START_LEAD_US             5000u
#define NOCTI_V3_TIMING_HISTORY_SLOTS           20u
#define NOCTI_V3_SLOT_DUMP_MAX_BYTES            8192u
#define NOCTI_V3_OPTICAL_HEALTH_PERIOD_MS       5000u

_Static_assert((NOCTI_V3_LED_COMPLETION_RING_CAPACITY &
                (NOCTI_V3_LED_COMPLETION_RING_CAPACITY - 1u)) == 0u,
               "LED completion ring must be a power of two");
_Static_assert(NOCTI_V3_LED_REQUEST_QUEUE_CAPACITY >= 2u,
               "LED request queue is too small");
_Static_assert(NOCTI_V3_I2C_ATOMIC_LEDOUT_BITS == 72u,
               "PCA LEDOUT burst is address plus seven data bytes with ACK bits");


/* Prompt 4 optical processing, completed-frame ownership, and fault policy. */
#define NOCTI_V3_SLOT_TIMING_RING_CAPACITY      64u
#define NOCTI_V3_PENDING_RAW_CAPACITY            256u
#define NOCTI_V3_MAX_RAW_SAMPLES_PER_SLOT        64u
#define NOCTI_V3_COMPLETED_FRAME_RING_CAPACITY   16u
#define NOCTI_V3_STACK_FRAME_SINK_BYTES          6144u
#define NOCTI_V3_PRIO_FRAME_SINK                 8u
#define NOCTI_V3_ADC_CONSECUTIVE_SPI_FATAL       3u
#define NOCTI_V3_ADC_CONSECUTIVE_CRC_FATAL       3u
#define NOCTI_V3_ADC_CONSECUTIVE_STATUS_FATAL    3u
#define NOCTI_V3_ADC_CONSECUTIVE_PERIOD_FATAL    4u
#define NOCTI_V3_ADC_DRDY_STOP_TIMEOUT_US        5000u
#define NOCTI_V3_STALE_CALLBACK_FATAL_THRESHOLD  3u
#define NOCTI_V3_SCHEDULER_FATAL_THRESHOLD       3u
#define NOCTI_V3_FRAME_SINK_WAIT_MS              20u
#define NOCTI_V3_PROCESSING_RESET_TIMEOUT_MS     1000u
#define NOCTI_V3_FRAME_ALL_SLOTS_MASK            ((1u << NOCTI_V3_SLOT_COUNT) - 1u)

_Static_assert((NOCTI_V3_SLOT_TIMING_RING_CAPACITY &
                (NOCTI_V3_SLOT_TIMING_RING_CAPACITY - 1u)) == 0u,
               "slot timing ring must be a power of two");
_Static_assert((NOCTI_V3_PENDING_RAW_CAPACITY &
                (NOCTI_V3_PENDING_RAW_CAPACITY - 1u)) == 0u,
               "pending raw ring must be a power of two");
_Static_assert((NOCTI_V3_COMPLETED_FRAME_RING_CAPACITY &
                (NOCTI_V3_COMPLETED_FRAME_RING_CAPACITY - 1u)) == 0u,
               "completed frame ring must be a power of two");
_Static_assert(NOCTI_V3_MAX_RAW_SAMPLES_PER_SLOT >=
                   (NOCTI_V3_SLOT_PERIOD_US / NOCTI_V3_ADS_PERIOD_US),
               "slot scratch must hold one nominal 5 ms slot");
_Static_assert(NOCTI_V3_SLOT_COUNT < 32u,
               "frame slot masks require fewer than 32 slots");

/* Prompt 2 ADS131M04 acquisition resources and validation policy. */
#define NOCTI_V3_ADS_CLKIN_HZ                  8192000u
#define NOCTI_V3_ADS_OSR                       512u
#define NOCTI_V3_ADS_CLOCK_REGISTER_8KSPS      0x0F0Au
#define NOCTI_V3_ADS_GAIN_REGISTER_X1          0x0000u
#define NOCTI_V3_ADS_CFG_CONTINUOUS            0x0600u
#define NOCTI_V3_ADS_FRAME_WORDS               6u
#define NOCTI_V3_ADS_WORD_BYTES                3u
#define NOCTI_V3_ADS_FRAME_BYTES               \
    (NOCTI_V3_ADS_FRAME_WORDS * NOCTI_V3_ADS_WORD_BYTES)
#define NOCTI_V3_DRDY_EVENT_RING_CAPACITY      128u
#define NOCTI_V3_RAW_RING_CAPACITY             1024u
#define NOCTI_V3_ADS_RATE_VALIDATION_SAMPLES   1024u
#define NOCTI_V3_ADS_SYNC_DISCARD_FRAMES       3u
#define NOCTI_V3_ADS_EXPECTED_RESYNC_BUDGET    1u
#define NOCTI_V3_ADS_RATE_TOLERANCE_PERCENT    5u
#define NOCTI_V3_ADS_CONTROL_TIMEOUT_MS        3000u
#define NOCTI_V3_ADS_INIT_TIMEOUT_MS           8000u
#define NOCTI_V3_ADS_EVENT_WAIT_TIMEOUT_MS     100u
#define NOCTI_V3_DARK_HEALTH_PERIOD_MS         5000u
#define NOCTI_V3_ADS_MAX_EVENT_BACKLOG         2u
#define NOCTI_V3_ADS_PERIOD_GAP_THRESHOLD_US   188u

_Static_assert((NOCTI_V3_DRDY_EVENT_RING_CAPACITY &
                (NOCTI_V3_DRDY_EVENT_RING_CAPACITY - 1u)) == 0u,
               "DRDY event ring capacity must be a power of two");
_Static_assert((NOCTI_V3_RAW_RING_CAPACITY &
                (NOCTI_V3_RAW_RING_CAPACITY - 1u)) == 0u,
               "raw sample ring capacity must be a power of two");
_Static_assert(NOCTI_V3_ADS_FRAME_BYTES == 18u,
               "ADS131M04 24-bit four-channel data frame must be 18 bytes");
_Static_assert(NOCTI_V3_ADS_SYNC_DISCARD_FRAMES >= 3u,
               "ADS sync recovery must discard at least three conversion periods");
_Static_assert(NOCTI_V3_ADS_EXPECTED_RESYNC_BUDGET >= 1u,
               "one host-issued SYNC resynchronization indication must be tolerated");
_Static_assert(NOCTI_V3_ADS_CLKIN_HZ / (2u * NOCTI_V3_ADS_OSR) ==
                   NOCTI_V3_ADS_SAMPLE_RATE_HZ,
               "8.192 MHz / (2*512) must equal 8 kSPS");

_Static_assert(NOCTI_V3_SLOT_COUNT * NOCTI_V3_SLOT_PERIOD_US ==
                   NOCTI_V3_FRAME_PERIOD_US,
               "20 slots must equal one 100 ms frame");
_Static_assert(NOCTI_V3_FRAME_RATE_HZ * NOCTI_V3_FRAME_PERIOD_US == 1000000u,
               "frame period must equal 10 Hz");
_Static_assert(NOCTI_V3_ON_BUDGET_US + NOCTI_V3_OFF_BUDGET_US ==
                   NOCTI_V3_SLOT_PERIOD_US,
               "ON and OFF budgets must equal one slot");
_Static_assert(NOCTI_V3_ADS_SAMPLE_RATE_HZ * NOCTI_V3_ADS_PERIOD_US == 1000000u,
               "ADS period must match 8 kSPS");
_Static_assert(NOCTI_V3_TARGET_WINDOW_ON_US >=
                   NOCTI_V3_K_TARGET_ON * NOCTI_V3_ADS_PERIOD_US,
               "ON target window must nominally contain K target");
_Static_assert(NOCTI_V3_TARGET_WINDOW_OFF_US >=
                   NOCTI_V3_K_TARGET_OFF * NOCTI_V3_ADS_PERIOD_US,
               "OFF target window must nominally contain K target");
_Static_assert(NOCTI_V3_MIN_WINDOW_ON_US >=
                   NOCTI_V3_K_MIN_ON * NOCTI_V3_ADS_PERIOD_US,
               "ON minimum window must nominally contain K minimum");
_Static_assert(NOCTI_V3_MIN_WINDOW_OFF_US >=
                   NOCTI_V3_K_MIN_OFF * NOCTI_V3_ADS_PERIOD_US,
               "OFF minimum window must nominally contain K minimum");
_Static_assert(NOCTI_V3_I2C_COMMIT_ALLOWANCE_US + NOCTI_V3_SETTLE_ON_US +
                   NOCTI_V3_TARGET_WINDOW_ON_US <= NOCTI_V3_ON_BUDGET_US,
               "initial ON timing budget is mathematically inconsistent");
_Static_assert(NOCTI_V3_I2C_COMMIT_ALLOWANCE_US + NOCTI_V3_SETTLE_OFF_US +
                   NOCTI_V3_TARGET_WINDOW_OFF_US + NOCTI_V3_END_GUARD_US <=
                   NOCTI_V3_OFF_BUDGET_US,
               "initial OFF timing budget is mathematically inconsistent");

typedef struct {
    uint32_t frame_rate_hz;
    uint32_t frame_period_us;
    uint32_t slot_count;
    uint32_t slot_period_us;
    uint32_t led_off_offset_us;
    uint32_t on_budget_us;
    uint32_t off_budget_us;
    uint32_t settle_on_us;
    uint32_t settle_off_us;
    uint32_t target_window_on_us;
    uint32_t target_window_off_us;
    uint32_t min_window_on_us;
    uint32_t min_window_off_us;
    uint32_t end_guard_us;
    uint32_t i2c_commit_allowance_us;
    uint32_t adc_sample_rate_hz;
    uint32_t adc_period_us;
    uint32_t ads_spi_clock_hz;
    uint32_t i2c_clock_hz;
    uint8_t k_target_on;
    uint8_t k_target_off;
    uint8_t k_min_on;
    uint8_t k_min_off;
} nocti_v3_timing_config_t;

typedef struct {
    uint8_t adc_core;
    uint8_t scheduler_core;
    uint8_t led_control_core;
    uint8_t optical_core;
    uint8_t led_recovery_core;
    uint8_t frame_sink_core;
    uint8_t network_core;
    uint8_t udp_stream_core;
    uint8_t health_core;
    uint8_t adc_priority;
    uint8_t scheduler_priority;
    uint8_t led_control_priority;
    uint8_t optical_priority;
    uint8_t led_recovery_priority;
    uint8_t frame_sink_priority;
    uint8_t udp_control_priority;
    uint8_t udp_stream_priority;
    uint8_t health_priority;
    uint32_t adc_stack_bytes;
    uint32_t scheduler_stack_bytes;
    uint32_t led_control_stack_bytes;
    uint32_t optical_stack_bytes;
    uint32_t led_recovery_stack_bytes;
    uint32_t frame_sink_stack_bytes;
    uint32_t udp_control_stack_bytes;
    uint32_t udp_stream_stack_bytes;
    uint32_t health_stack_bytes;
} nocti_v3_task_plan_t;

const nocti_v3_timing_config_t *nocti_v3_timing_config(void);
const nocti_v3_task_plan_t *nocti_v3_task_plan(void);
bool nocti_v3_timing_runtime_validate(void);

#ifdef __cplusplus
}
#endif
