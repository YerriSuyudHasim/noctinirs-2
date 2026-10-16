// ============================================================
// File: main/app_main_fnirs_test.c
// NoctiNIRS fNIRS-only validation firmware
//
// Modes:
// - DARK   : all LEDs remain OFF; all 20 slots measure LED-off/ambient signal
// - SINGLE : only one selected slot turns ON; the other 19 slots remain LED-off
// - SCAN   : all 20 slots run normally (10 sources x 2 wavelengths)
//
// UDP control commands (port 3333):
// - START
// - STOP
// - MODE DARK
// - MODE SINGLE
// - MODE SCAN
// - SLOT <0..19>
// - CURRENT 735 <0..FNIRS_LED_CURRENT_LIMIT_MA>
// - CURRENT 850 <0..FNIRS_LED_CURRENT_LIMIT_MA>
// - STATUS
// - REGS
// - HELP
//
// UDP stream output (port 3334):
// - NOCTI_REC_SESSION
// - NOCTI_REC_TIME64
// - NOCTI_REC_FNIRS_CFG
// - NOCTI_REC_EVENT (test-mode metadata)
// - NOCTI_REC_FNIRS_FRAME (paired v2: one LED-ON and one LED-OFF phase record per frame)
// ============================================================

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_intr_alloc.h"
#include "driver/gpio.h"

#include "nocti_board.h"
#include "nocti_fnirs.h"
#include "nocti_led_driver.h"
#include "nocti_log.h"
#include "nocti_scheduler.h"
#include "nocti_stream.h"
#include "nocti_timebase.h"
#include "nocti_wifi.h"

static const char *TAG = "app_fnirs_test";

// ============================================================
// fNIRS acquisition configuration
// ============================================================

#define FNIRS_FRAME_FS_HZ              (10u)
#define FNIRS_SLOT_COUNT               (20u)
#define FNIRS_SOURCE_COUNT             (10u)

#define FNIRS_TICK_PERIOD_US           (5000u)
#define FNIRS_FRAME_PERIOD_US          (100000u)

// Paired per-slot acquisition with absolute command deadlines at 8 kSPS:
//   1) issue LED-ON at the tick edge;
//   2) issue LED-OFF at the fixed tick + 2500 us deadline;
//   3) PCA9956B MODE2.OCH=0 commits LEDOUT changes on the I2C STOP;
//   4) timestamp both optical edges after the successful I2C transaction;
//   5) accept ADC samples at least 500 us after the actual optical edge.
//
// The processor targets ten contiguous samples (1250 us at 8 kSPS), but a slot
// remains scientifically usable with at least eight contiguous samples
// (1000 us). The scheduler admission test therefore protects the minimum-valid
// window instead of aborting a slot merely because the target K=10 window is no
// longer guaranteed. Actual n_used remains embedded in every phase record.
#define FNIRS_K_ON_TARGET              (10u)
#define FNIRS_K_OFF_TARGET             (10u)
#define FNIRS_K_ON_MIN_VALID           (8u)
#define FNIRS_K_OFF_MIN_VALID          (8u)
#define FNIRS_SETTLE_US                (500u)
#define FNIRS_TARGET_WINDOW_US         (1250u)
#define FNIRS_MIN_VALID_WINDOW_US      (1000u)
#define FNIRS_ON_TIME_US               (2500u)
#define FNIRS_OFF_GAP_US               (2500u)
#define FNIRS_OFF_MIN_SETTLE_US        FNIRS_SETTLE_US

#define FNIRS_SCHED_END_GUARD_US        (400u)
#define FNIRS_SCHED_LATE_WARN_US        (200u)
#define FNIRS_I2C_CLOCK_HZ              (400000u)
#define FNIRS_I2C_ASYNC_QUEUE_DEPTH      (4u)
#define FNIRS_ADS131_SPI_CLOCK_HZ        (8000000u)
#define FNIRS_LED_INIT_I2C_TIMEOUT_MS   (50u)
#define FNIRS_LED_RUN_I2C_TIMEOUT_MS    (2u)
#define FNIRS_LED_RESET_LOW_US           (5u)
#define FNIRS_LED_RESET_RECOVERY_US      (1600u)

#define FNIRS_DEFAULT_CURRENT_735_MA    (15u)
#define FNIRS_DEFAULT_CURRENT_850_MA    (15u)

// Conservative software ceiling for validation. Raise only after confirming
// the optical and thermal safety limits of the complete hardware assembly.
#define FNIRS_LED_CURRENT_LIMIT_MA      (50u)

// ADS131M04 CLOCK register used by this project:
// channels 0..3 enabled, high-resolution mode,
// OSR = 512, 8000 SPS at 8.192 MHz CLKIN.
#define ADS131_REG_MODE_ADDR            (0x02u)
#define ADS131_REG_CLOCK_ADDR           (0x03u)
#define ADS131_REG_GAIN_ADDR            (0x04u)
#define ADS131_REG_CFG_ADDR             (0x06u)
#define ADS131_CLOCK_8000_SPS           (0x0F0Au)
#define ADS131_OSR                      (512u)
#define ADS131_GAIN_ALL_X1              (0x0000u)
#define ADS131_CFG_CONTINUOUS_DEFAULT   (0x0600u)
#define ADS131_CFG_GC_EN_BIT            (1u << 8)
#define ADS131_MODE_WLENGTH_MASK        (0x0300u)
#define ADS131_MODE_WLENGTH_24BIT       (0x0100u)
#define ADS131_MODE_RX_CRC_MASK         (0x1000u)
#define ADS131_MODE_CRC_TYPE_MASK       (0x0800u)
#define ADS131_MODE_DRDY_HIZ_MASK       (0x0002u)
#define ADS131_MODE_DRDY_FMT_MASK       (0x0001u)
#define ADS131_MODE_DRDY_FMT_PULSE      (0x0001u)
#define ADS131_SAMPLE_RATE_HZ           (8000u)
#define ADS131_EXPECTED_PERIOD_US        (125u)
#define ADS131_RATE_VALIDATION_FRAMES   (1024u)
#define ADS131_RATE_TOLERANCE_PERCENT    (5u)
#define FNIRS_TARGET_WINDOW_NOMINAL_SAMPLES \
    ((FNIRS_TARGET_WINDOW_US * ADS131_SAMPLE_RATE_HZ) / 1000000u)
#define FNIRS_MIN_WINDOW_NOMINAL_SAMPLES \
    ((FNIRS_MIN_VALID_WINDOW_US * ADS131_SAMPLE_RATE_HZ) / 1000000u)

_Static_assert(FNIRS_SLOT_COUNT * FNIRS_TICK_PERIOD_US ==
               FNIRS_FRAME_PERIOD_US,
               "20 optical slots must equal one 100 ms frame");
_Static_assert(FNIRS_SETTLE_US + FNIRS_MIN_VALID_WINDOW_US <= FNIRS_ON_TIME_US,
               "ON budget must contain settle plus minimum-valid window");
_Static_assert(FNIRS_SETTLE_US + FNIRS_MIN_VALID_WINDOW_US <= FNIRS_OFF_GAP_US,
               "OFF budget must contain settle plus minimum-valid window");
_Static_assert(FNIRS_SETTLE_US + FNIRS_TARGET_WINDOW_US <= FNIRS_ON_TIME_US,
               "ON budget should also fit the target K window when timing is healthy");
_Static_assert(FNIRS_SETTLE_US + FNIRS_TARGET_WINDOW_US <= FNIRS_OFF_GAP_US,
               "OFF budget should also fit the target K window when timing is healthy");
_Static_assert(FNIRS_ON_TIME_US + FNIRS_OFF_GAP_US == FNIRS_TICK_PERIOD_US,
               "optical ON and OFF budgets must equal one slot tick");
_Static_assert(FNIRS_TARGET_WINDOW_NOMINAL_SAMPLES >= FNIRS_K_ON_TARGET,
               "target ADC window must contain target K fully settled samples");
_Static_assert(FNIRS_MIN_WINDOW_NOMINAL_SAMPLES >= FNIRS_K_ON_MIN_VALID,
               "minimum ADC window must contain minimum-valid K samples");
_Static_assert(FNIRS_K_ON_TARGET >= FNIRS_K_ON_MIN_VALID,
               "target K must be greater than or equal to minimum-valid K");
_Static_assert(FNIRS_K_ON_TARGET == FNIRS_K_OFF_TARGET,
               "paired subtraction targets equal ON and OFF sample counts");
_Static_assert(FNIRS_K_ON_MIN_VALID == FNIRS_K_OFF_MIN_VALID,
               "paired subtraction requires equal ON and OFF validity thresholds");
_Static_assert(ADS131_SAMPLE_RATE_HZ * ADS131_EXPECTED_PERIOD_US == 1000000u,
               "ADS131 sample rate and expected period must agree exactly");

// Continuous ADC ring buffer. The producer writes complete entries, then
// advances g_adc_wr. The scheduler reads only indices below a write snapshot.
#define FNIRS_ADC_RB_SIZE               (2048u)
#define FNIRS_ADC_SEARCH_BACK           FNIRS_ADC_RB_SIZE
#define FNIRS_ADC_COMMIT_WAIT_US         (3000u)
#define FNIRS_LED_EDGE_TIMESTAMP_MAX_US  (2000u)

#define FNIRS_PLATFORM_INIT_STACK_BYTES (4096u)
#define FNIRS_PLATFORM_INIT_PRIORITY    (20u)
#define FNIRS_VALIDATION_STACK_BYTES    (4096u)
#define FNIRS_LED_CONTROL_STACK_BYTES   (6144u)
#define FNIRS_VALIDATION_PRIORITY       (19u)
#define FNIRS_ADC_TASK_STACK_BYTES      (4096u)
#define FNIRS_PROCESS_QUEUE_LEN          (64u)
#define FNIRS_PROCESS_TASK_STACK_BYTES   (6144u)
#define FNIRS_PROCESS_TASK_PRIORITY      (11u)
#define FNIRS_PROCESS_CORE               (0)
#define FNIRS_PROCESS_DRAIN_TIMEOUT_MS   (100u)

/*
 * ADS131 stays one priority level above the optical scheduler. At 8 kSPS the
 * producer must drain each 125-us DRDY frame continuously to protect the ADC's
 * two-deep output FIFO. The scheduler now admits a slot against the K_min=8
 * window, leaving 1000 us for dispatch plus the LED-ON transaction, while the
 * processing task still targets K=10 whenever the physical window allows it.
 */
#define FNIRS_ADC_TASK_PRIORITY         (18u)
#define FNIRS_SCHED_TASK_PRIORITY       (17u)
// The I2C ISR captures a post-STOP completion timestamp, so the scheduler no longer
// needs to preempt the ADC task merely to timestamp an LED edge.

/*
 * Wi-Fi tasks saat ini dipasang pada core 0.
 * Tempatkan ADC dan optical scheduler pada core 1.
 */
#define FNIRS_ACQ_CORE                  (1)

#define FNIRS_STOP_SETTLE_MS            (10u)
#define FNIRS_START_PREFILL_MS          (10u)

/*
 * Runtime serial policy (v16)
 * ---------------------------
 * ESP-IDF console writes can block while bytes are placed into the UART or
 * USB Serial/JTAG FIFO. During acquisition, preserve ERROR logs but suppress
 * INFO/WARN output from the timing-critical NoctiNIRS modules. Full status is
 * still available over UDP and a compact summary is printed only after STOP.
 */
#define FNIRS_RUNTIME_SERIAL_QUIET       (1u)
#define FNIRS_RUNTIME_HEALTH_PERIOD_MS   (10000u)
#define FNIRS_DIAG_TRACE_CAPACITY        (128u)
#define FNIRS_DIAG_TRACE_DUMP_MAX        (32u)

#define FNIRS_EVENT_CODE_TEST_CONFIG    (0xF100u)

// ============================================================
// Test state
// ============================================================

typedef enum {
    FNIRS_TEST_MODE_DARK = 0,
    FNIRS_TEST_MODE_SINGLE = 1,
    FNIRS_TEST_MODE_SCAN = 2,
} fnirs_test_mode_t;

typedef struct {
    nocti_time_us_t ts_us;
    uint32_t drdy_sequence;      // logical conversion sequence derived from IRQ + time
    uint32_t irq_sequence;       // raw GPIO event sequence captured by the driver
    int32_t ch[4];
    bool crc_ok;
    bool timing_ok;
} fnirs_adc_frame_t;

typedef struct {
    uint32_t generation;
    uint32_t frame_index;
    uint32_t scheduler_slot_index;
    uint8_t slot_index;
    bool aborted;
    bool force_partial;
    bool valid;
    bool complete;
    nocti_time_us_t timestamp_us;
    nocti_time_us_t off_t0_us;
    nocti_time_us_t off_t1_us;
    nocti_time_us_t on_t0_us;
    nocti_time_us_t on_t1_us;
} fnirs_slot_job_t;

typedef enum {
    FNIRS_TRACE_START = 1,
    FNIRS_TRACE_ADC_READ_ERROR = 2,
    FNIRS_TRACE_ADC_PERIOD_GAP = 3,
    FNIRS_TRACE_SCHED_ABORT = 4,
    FNIRS_TRACE_STOP = 5,
} fnirs_diag_trace_type_t;

typedef struct {
    volatile uint32_t commit_sequence;
    uint32_t type;
    nocti_time_us_t timestamp_us;
    uint32_t value0;
    uint32_t value1;
} fnirs_diag_trace_event_t;

typedef struct {
    const char *tag;
    esp_log_level_t saved_level;
} fnirs_runtime_log_tag_t;

static nocti_board_t g_board;
static nocti_stream_t g_stream;
static nocti_wifi_t g_wifi;

// Control replies can exceed 1 kB after the expanded timing diagnostics.
// Keep the buffer in BSS instead of the UDP-control task stack.
#define FNIRS_CTRL_RESPONSE_BYTES       (4096u)
static char g_ctrl_response[FNIRS_CTRL_RESPONSE_BYTES];
static SemaphoreHandle_t g_ctrl_response_mutex = NULL;

static nocti_slot_t g_slots[FNIRS_SLOT_COUNT];
static TaskHandle_t g_fnirs_adc_task = NULL;
static TaskHandle_t g_fnirs_process_task = NULL;
static QueueHandle_t g_fnirs_process_queue = NULL;
static SemaphoreHandle_t g_fnirs_process_mutex = NULL;

static volatile bool g_run = false;
static volatile bool g_fnirs_adc_run = false;

// These settings are changed only while acquisition is stopped.
static volatile fnirs_test_mode_t g_test_mode = FNIRS_TEST_MODE_DARK;
static volatile uint8_t g_selected_slot = 0u;
static volatile uint16_t g_current_735_ma = FNIRS_DEFAULT_CURRENT_735_MA;
static volatile uint16_t g_current_850_ma = FNIRS_DEFAULT_CURRENT_850_MA;

static uint32_t g_session_id = 0u;

static volatile bool g_runtime_serial_quiet = false;
static volatile uint32_t g_runtime_health_serial_suppressed = 0u;
static volatile uint32_t g_diag_trace_write_count = 0u;
static fnirs_diag_trace_event_t g_diag_trace[FNIRS_DIAG_TRACE_CAPACITY];

static fnirs_runtime_log_tag_t g_runtime_log_tags[] = {
    { "app_fnirs_test", ESP_LOG_INFO },
    { "nocti_wifi", ESP_LOG_INFO },
    { "nocti_timebase", ESP_LOG_INFO },
    { "nocti_sched", ESP_LOG_INFO },
    { "nocti_fnirs", ESP_LOG_INFO },
    { "nocti_led", ESP_LOG_INFO },
    { "nocti_hal_i2c", ESP_LOG_INFO },
    { "nocti_hal_spi", ESP_LOG_INFO },
    { "nocti_stream", ESP_LOG_INFO },
    { "nocti_board", ESP_LOG_INFO },
    { "wifi", ESP_LOG_INFO },
};

// ============================================================
// ADC producer state and counters
// ============================================================

static fnirs_adc_frame_t g_adc_rb[FNIRS_ADC_RB_SIZE];
static volatile uint32_t g_adc_wr = 0u;

static volatile uint32_t g_adc_ok_cnt = 0u;
static volatile uint32_t g_adc_err_cnt = 0u;
static volatile uint32_t g_adc_crc_bad_cnt = 0u;
static volatile uint32_t g_adc_max_gap_us = 0u;
static volatile uint32_t g_adc_missed_drdy_count = 0u;
static volatile uint32_t g_adc_max_sequence_gap = 0u;
static volatile uint32_t g_adc_validated_sps = 0u;
static volatile uint32_t g_adc_validation_min_period_us = 0u;
static volatile uint32_t g_adc_validation_max_period_us = 0u;
static nocti_time_us_t g_adc_last_ts_us = 0;
static uint32_t g_adc_last_drdy_sequence = 0u;
static uint32_t g_adc_logical_sequence = 0u;
static volatile uint32_t g_adc_duplicate_irq_count = 0u;
static volatile uint32_t g_adc_nonmonotonic_ts_count = 0u;
static volatile uint32_t g_adc_period_gap_count = 0u;
static volatile int8_t g_platform_init_core = -1;
static volatile int8_t g_process_task_core = -1;
static volatile uint32_t g_led_err_cnt = 0u;
static volatile bool g_led_fatal_fault = false;
static volatile uint32_t g_stream_err_cnt = 0u;
static volatile uint32_t g_frame_count = 0u;
static volatile uint32_t g_partial_frame_count = 0u;
static volatile uint32_t g_under_k_on_slot_count = 0u;
static volatile uint32_t g_under_k_off_slot_count = 0u;
static volatile uint32_t g_subtarget_on_slot_count = 0u;
static volatile uint32_t g_subtarget_off_slot_count = 0u;
static volatile uint32_t g_slot_abort_count = 0u;
static volatile uint32_t g_process_queue_drop_count = 0u;
static volatile uint32_t g_process_job_count = 0u;
static volatile uint32_t g_process_adc_wait_timeout_count = 0u;
static volatile bool g_process_busy = false;
static volatile uint32_t g_acq_generation = 0u;

static volatile uint32_t g_led_on_max_us = 0u;
static volatile uint32_t g_led_off_max_us = 0u;
static volatile uint32_t g_led_on_count = 0u;
static volatile uint32_t g_led_off_count = 0u;
static volatile uint32_t g_off_dark_min_us = UINT32_MAX;
static volatile uint32_t g_on_light_min_us = UINT32_MAX;
static volatile uint32_t g_off_stable_min_us = UINT32_MAX;
static volatile uint32_t g_on_stable_min_us = UINT32_MAX;
static volatile uint8_t g_on_min_n_used = UINT8_MAX;
static volatile uint8_t g_off_min_n_used = UINT8_MAX;

// ============================================================
// fNIRS frame accumulator
// ============================================================

static nocti_payload_fnirs_frame_v1_t g_fnirs_on_frame;
static nocti_payload_fnirs_frame_v1_t g_fnirs_off_frame;
static uint16_t g_fnirs_frame_id = 0u;
static nocti_time_us_t g_led_off_ready_us = 0;
static fnirs_slot_job_t g_pending_slot_job;
static bool g_frame_open = false;
static bool g_force_partial_frame = false;
static bool g_scheduler_frame_seen = false;
static uint32_t g_scheduler_frame_index = 0u;

// ============================================================
// Small helpers
// ============================================================

static const char *test_mode_name(fnirs_test_mode_t mode)
{
    switch (mode) {
        case FNIRS_TEST_MODE_DARK:
            return "DARK";
        case FNIRS_TEST_MODE_SINGLE:
            return "SINGLE";
        case FNIRS_TEST_MODE_SCAN:
            return "SCAN";
        default:
            return "UNKNOWN";
    }
}

static uint32_t popcount32(uint32_t value)
{
    uint32_t count = 0u;
    while (value != 0u) {
        count += value & 1u;
        value >>= 1u;
    }
    return count;
}

static bool slot_led_is_active(uint8_t slot_index)
{
    const fnirs_test_mode_t mode = g_test_mode;

    if (mode == FNIRS_TEST_MODE_SCAN) {
        return true;
    }

    if (mode == FNIRS_TEST_MODE_SINGLE) {
        return slot_index == g_selected_slot;
    }

    return false;
}

static uint8_t slot_index_from_event(const nocti_sched_event_t *ev)
{
    return NOCTI_SLOT_INDEX(ev->slot.led_id, ev->slot.wavelength);
}

static const char *diag_trace_type_name(uint32_t type)
{
    switch ((fnirs_diag_trace_type_t)type) {
        case FNIRS_TRACE_START: return "START";
        case FNIRS_TRACE_ADC_READ_ERROR: return "ADC_ERR";
        case FNIRS_TRACE_ADC_PERIOD_GAP: return "ADC_GAP";
        case FNIRS_TRACE_SCHED_ABORT: return "SCHED_ABORT";
        case FNIRS_TRACE_STOP: return "STOP";
        default: return "UNKNOWN";
    }
}

static void diag_trace_reset(void)
{
    memset(g_diag_trace, 0, sizeof(g_diag_trace));
    __atomic_store_n(&g_diag_trace_write_count, 0u, __ATOMIC_RELEASE);
}

static void diag_trace_push(fnirs_diag_trace_type_t type,
                            uint32_t value0,
                            uint32_t value1)
{
    const uint32_t sequence =
        __atomic_fetch_add(&g_diag_trace_write_count, 1u, __ATOMIC_RELAXED) + 1u;
    fnirs_diag_trace_event_t *event =
        &g_diag_trace[(sequence - 1u) % FNIRS_DIAG_TRACE_CAPACITY];

    // Mark the slot uncommitted while it is being overwritten. Producers use
    // unique indices, and consumers inspect the ring only after STOP.
    __atomic_store_n(&event->commit_sequence, 0u, __ATOMIC_RELAXED);
    event->type = (uint32_t)type;
    event->timestamp_us = nocti_timebase_now_us();
    event->value0 = value0;
    event->value1 = value1;
    __atomic_store_n(&event->commit_sequence, sequence, __ATOMIC_RELEASE);
}

static void runtime_serial_set_quiet(bool quiet)
{
#if FNIRS_RUNTIME_SERIAL_QUIET
    if (quiet == g_runtime_serial_quiet) {
        return;
    }

    const size_t tag_count =
        sizeof(g_runtime_log_tags) / sizeof(g_runtime_log_tags[0]);

    if (quiet) {
        for (size_t i = 0u; i < tag_count; i++) {
#if defined(CONFIG_LOG_DYNAMIC_LEVEL_CONTROL) && CONFIG_LOG_DYNAMIC_LEVEL_CONTROL
            g_runtime_log_tags[i].saved_level =
                esp_log_level_get(g_runtime_log_tags[i].tag);
#else
            g_runtime_log_tags[i].saved_level = ESP_LOG_INFO;
#endif
            // Keep fatal runtime diagnostics visible without allowing long
            // INFO/WARN formatting and console writes during acquisition.
            esp_log_level_set(g_runtime_log_tags[i].tag, ESP_LOG_ERROR);
        }
        g_runtime_serial_quiet = true;
    } else {
        for (size_t i = 0u; i < tag_count; i++) {
            esp_log_level_set(g_runtime_log_tags[i].tag,
                              g_runtime_log_tags[i].saved_level);
        }
        g_runtime_serial_quiet = false;
    }
#else
    (void)quiet;
#endif
}

static void reset_runtime_counters(void)
{
    diag_trace_reset();
    g_runtime_health_serial_suppressed = 0u;
    g_adc_ok_cnt = 0u;
    g_adc_err_cnt = 0u;
    g_adc_crc_bad_cnt = 0u;
    g_led_err_cnt = 0u;
    g_led_fatal_fault = false;
    g_stream_err_cnt = 0u;
    g_frame_count = 0u;
    g_partial_frame_count = 0u;
    g_under_k_on_slot_count = 0u;
    g_under_k_off_slot_count = 0u;
    g_subtarget_on_slot_count = 0u;
    g_subtarget_off_slot_count = 0u;
    g_slot_abort_count = 0u;
    g_process_queue_drop_count = 0u;
    g_process_job_count = 0u;
    g_process_adc_wait_timeout_count = 0u;
    g_process_busy = false;
    g_adc_max_gap_us = 0u;
    g_adc_missed_drdy_count = 0u;
    g_adc_max_sequence_gap = 0u;
    g_adc_last_ts_us = 0;
    g_adc_last_drdy_sequence = 0u;
    g_adc_logical_sequence = 0u;
    g_adc_duplicate_irq_count = 0u;
    g_adc_nonmonotonic_ts_count = 0u;
    g_adc_period_gap_count = 0u;
    nocti_fnirs_reset_stats();
    g_led_on_max_us = 0u;
    g_led_off_max_us = 0u;
    g_led_on_count = 0u;
    g_led_off_count = 0u;
    g_off_dark_min_us = UINT32_MAX;
    g_on_light_min_us = UINT32_MAX;
    g_off_stable_min_us = UINT32_MAX;
    g_on_stable_min_us = UINT32_MAX;
    g_on_min_n_used = UINT8_MAX;
    g_off_min_n_used = UINT8_MAX;
    nocti_led_driver_reset_stats();
}

static void fnirs_phase_frame_reset(nocti_payload_fnirs_frame_v1_t *frame,
                                    bool led_off_phase)
{
    if (!frame) return;
    memset(frame, 0, sizeof(*frame));
    frame->version = NOCTI_PAYLOAD_FNIRS_FRAME_V2_VERSION;
    frame->flags = NOCTI_FNIRS_FRAME_F_PAIRED |
                   (led_off_phase ? NOCTI_FNIRS_FRAME_F_LED_OFF : 0u);
    frame->frame_id = g_fnirs_frame_id;
}

static void fnirs_frame_reset(void)
{
    fnirs_phase_frame_reset(&g_fnirs_on_frame, false);
    fnirs_phase_frame_reset(&g_fnirs_off_frame, true);
}

static void fnirs_store_phase_slot(nocti_payload_fnirs_frame_v1_t *frame,
                                   uint8_t slot_index,
                                   uint8_t n_used,
                                   const int32_t average[4],
                                   bool any_crc_bad,
                                   uint8_t required_k)
{
    if (!frame || slot_index >= FNIRS_SLOT_COUNT) return;

    const uint32_t slot_bit = (1u << slot_index);
    frame->n_used[slot_index] = n_used;

    if (n_used > 0u && average) {
        frame->avg[slot_index][0] = average[0];
        frame->avg[slot_index][1] = average[1];
        frame->avg[slot_index][2] = average[2];
        frame->avg[slot_index][3] = average[3];
        frame->valid_mask |= slot_bit;
    }
    if (n_used < required_k) frame->underk_mask |= slot_bit;
    if (any_crc_bad) frame->crc_mask |= slot_bit;
}

static void send_ctrlf(nocti_wifi_t *wifi,
                       const nocti_wifi_peer_t *peer,
                       const char *fmt,
                       ...) __attribute__((format(printf, 3, 4)));

static void send_ctrlf(nocti_wifi_t *wifi,
                       const nocti_wifi_peer_t *peer,
                       const char *fmt,
                       ...)
{
    if (!wifi || !peer || !fmt) {
        return;
    }

    if (!g_ctrl_response_mutex) {
        return;
    }

    if (xSemaphoreTake(g_ctrl_response_mutex, pdMS_TO_TICKS(100u)) != pdTRUE) {
        NOCTI_LOGW(TAG, "control response buffer busy");
        return;
    }

    va_list args;
    va_start(args, fmt);
    const int written = vsnprintf(g_ctrl_response,
                                  sizeof(g_ctrl_response),
                                  fmt,
                                  args);
    va_end(args);

    if (written > 0) {
        size_t len = (size_t)written;
        if (len >= sizeof(g_ctrl_response)) {
            len = sizeof(g_ctrl_response) - 1u;
        }

        const esp_err_t err = nocti_wifi_send_ctrl_to(wifi,
                                                       peer,
                                                       g_ctrl_response,
                                                       len);
        if (err != ESP_OK) {
            NOCTI_LOGW(TAG, "control reply send failed: %s", esp_err_to_name(err));
        }
    }

    xSemaphoreGive(g_ctrl_response_mutex);
}

static size_t copy_normalized_command(char *dst,
                                      size_t dst_capacity,
                                      const uint8_t *src,
                                      size_t src_len)
{
    if (!dst || dst_capacity == 0u || !src || src_len == 0u) {
        return 0u;
    }

    size_t n = src_len;
    if (n >= dst_capacity) {
        n = dst_capacity - 1u;
    }

    memcpy(dst, src, n);
    dst[n] = '\0';

    size_t first = 0u;
    while (first < n && isspace((unsigned char)dst[first])) {
        first++;
    }

    size_t last = n;
    while (last > first && isspace((unsigned char)dst[last - 1u])) {
        last--;
    }

    const size_t trimmed_len = last - first;
    if (first > 0u && trimmed_len > 0u) {
        memmove(dst, &dst[first], trimmed_len);
    }
    dst[trimmed_len] = '\0';

    for (size_t i = 0u; i < trimmed_len; i++) {
        dst[i] = (char)toupper((unsigned char)dst[i]);
    }

    return trimmed_len;
}

static bool parse_u32_argument(const char *text,
                               const char *prefix,
                               uint32_t *out_value)
{
    if (!text || !prefix || !out_value) {
        return false;
    }

    const size_t prefix_len = strlen(prefix);
    if (strncmp(text, prefix, prefix_len) != 0) {
        return false;
    }

    const char *number_text = text + prefix_len;
    if (*number_text == '\0') {
        return false;
    }

    char *end = NULL;
    const unsigned long parsed = strtoul(number_text, &end, 10);
    if (end == number_text) {
        return false;
    }

    while (*end != '\0' && isspace((unsigned char)*end)) {
        end++;
    }

    if (*end != '\0' || parsed > UINT32_MAX) {
        return false;
    }

    *out_value = (uint32_t)parsed;
    return true;
}

// ============================================================
// LED current and slot configuration
// ============================================================

static void build_slot_table(void)
{
    uint8_t index = 0u;

    for (uint8_t led = 0u; led < FNIRS_SOURCE_COUNT; led++) {
        g_slots[index].led_id = led;
        g_slots[index].wavelength = NOCTI_WL_735;
        g_slots[index].on_time_us = FNIRS_ON_TIME_US;
        g_slots[index].settle_us = FNIRS_SETTLE_US;
        g_slots[index].sample_window_us = FNIRS_MIN_VALID_WINDOW_US;
        g_slots[index].off_gap_us = FNIRS_OFF_GAP_US;
        g_slots[index].current_mA = g_current_735_ma;
        index++;

        g_slots[index].led_id = led;
        g_slots[index].wavelength = NOCTI_WL_850;
        g_slots[index].on_time_us = FNIRS_ON_TIME_US;
        g_slots[index].settle_us = FNIRS_SETTLE_US;
        g_slots[index].sample_window_us = FNIRS_MIN_VALID_WINDOW_US;
        g_slots[index].off_gap_us = FNIRS_OFF_GAP_US;
        g_slots[index].current_mA = g_current_850_ma;
        index++;
    }
}

static esp_err_t apply_led_currents(uint16_t current_735_ma,
                                    uint16_t current_850_ma)
{
    if (current_735_ma > FNIRS_LED_CURRENT_LIMIT_MA ||
        current_850_ma > FNIRS_LED_CURRENT_LIMIT_MA) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t slot_currents[FNIRS_SLOT_COUNT];

    for (uint8_t slot = 0u; slot < FNIRS_SLOT_COUNT; slot++) {
        const bool is_735 = ((slot & 1u) == 0u);
        slot_currents[slot] = is_735 ? current_735_ma : current_850_ma;
    }

    const esp_err_t err = nocti_led_driver_apply_slot_currents_ma(slot_currents);
    if (err == ESP_OK) {
        for (uint8_t slot = 0u; slot < FNIRS_SLOT_COUNT; slot++) {
            g_slots[slot].current_mA = slot_currents[slot];
        }
        g_current_735_ma = current_735_ma;
        g_current_850_ma = current_850_ma;
    }

    return err;
}

static void log_led_mapping(void)
{
    uint8_t map[NOCTI_LED_SLOTS];
    const esp_err_t err = nocti_led_driver_get_resolved_slot_map(map);
    if (err != ESP_OK) {
        NOCTI_LOGW(TAG, "unable to read LED map: %s", esp_err_to_name(err));
        return;
    }

    for (uint8_t slot = 0u; slot < NOCTI_LED_SLOTS; slot++) {
        const uint16_t wavelength_nm = ((slot & 1u) == 0u) ? 735u : 850u;
        NOCTI_LOGI(TAG,
                   "slot=%u led=%u wl=%u nm -> PCA ch=%u",
                   (unsigned)slot,
                   (unsigned)(slot / 2u),
                   (unsigned)wavelength_nm,
                   (unsigned)map[slot]);
    }
}

// ============================================================
// ADS131M04 validation helpers
// ============================================================

static esp_err_t read_ads131_registers(uint16_t *mode,
                                       uint16_t *clock,
                                       uint16_t *gain,
                                       uint16_t *cfg)
{
    if (!mode || !clock || !gain || !cfg) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = nocti_fnirs_dbg_read_reg_u16(ADS131_REG_MODE_ADDR, mode);
    if (err != ESP_OK) {
        return err;
    }

    err = nocti_fnirs_dbg_read_reg_u16(ADS131_REG_CLOCK_ADDR, clock);
    if (err != ESP_OK) {
        return err;
    }

    err = nocti_fnirs_dbg_read_reg_u16(ADS131_REG_GAIN_ADDR, gain);
    if (err != ESP_OK) {
        return err;
    }

    return nocti_fnirs_dbg_read_reg_u16(ADS131_REG_CFG_ADDR, cfg);
}

static esp_err_t validate_ads131_registers(void)
{
    uint16_t mode = 0u;
    uint16_t clock = 0u;
    uint16_t gain = 0u;
    uint16_t cfg = 0u;

    const esp_err_t err = read_ads131_registers(&mode, &clock, &gain, &cfg);
    if (err != ESP_OK) {
        NOCTI_LOGE(TAG, "ADS131 register readback failed: %s", esp_err_to_name(err));
        return err;
    }

    NOCTI_LOGI(TAG,
               "ADS131 readback MODE=0x%04X CLOCK=0x%04X GAIN=0x%04X CFG=0x%04X",
               (unsigned)mode,
               (unsigned)clock,
               (unsigned)gain,
               (unsigned)cfg);

    if ((mode & ADS131_MODE_WLENGTH_MASK) != ADS131_MODE_WLENGTH_24BIT) {
        NOCTI_LOGE(TAG,
                   "ADS131 MODE WLENGTH mismatch: expected 24-bit field 0x%04X, read MODE=0x%04X",
                   (unsigned)ADS131_MODE_WLENGTH_24BIT,
                   (unsigned)mode);
        return ESP_ERR_INVALID_STATE;
    }

    if ((mode & ADS131_MODE_RX_CRC_MASK) != 0u ||
        (mode & ADS131_MODE_CRC_TYPE_MASK) != 0u ||
        (mode & ADS131_MODE_DRDY_HIZ_MASK) != 0u ||
        (mode & ADS131_MODE_DRDY_FMT_MASK) != ADS131_MODE_DRDY_FMT_PULSE) {
        NOCTI_LOGE(TAG,
                   "ADS131 MODE protocol mismatch: require CRC-IN off, CCITT, "
                   "push-pull DRDY pulse; read MODE=0x%04X",
                   (unsigned)mode);
        return ESP_ERR_INVALID_STATE;
    }

    if (clock != ADS131_CLOCK_8000_SPS) {
        NOCTI_LOGE(TAG,
                   "ADS131 CLOCK mismatch: expected 0x%04X, read 0x%04X",
                   (unsigned)ADS131_CLOCK_8000_SPS,
                   (unsigned)clock);
        return ESP_ERR_INVALID_STATE;
    }

    if (gain != ADS131_GAIN_ALL_X1) {
        NOCTI_LOGE(TAG,
                   "ADS131 GAIN mismatch: expected all channels x1 (0x%04X), read 0x%04X",
                   (unsigned)ADS131_GAIN_ALL_X1,
                   (unsigned)gain);
        return ESP_ERR_INVALID_STATE;
    }

    if (cfg != ADS131_CFG_CONTINUOUS_DEFAULT ||
        (cfg & ADS131_CFG_GC_EN_BIT) != 0u) {
        NOCTI_LOGE(TAG,
                   "ADS131 CFG mismatch: expected continuous conversion 0x%04X with GC_EN=0, read 0x%04X",
                   (unsigned)ADS131_CFG_CONTINUOUS_DEFAULT,
                   (unsigned)cfg);
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

static esp_err_t validate_drdy_rate_once(void)
{
    // This function is executed by a temporary task pinned to the acquisition
    // core. The same task is the permanent DRDY consumer for this validation
    // burst, so GPIO ISR wakeups and SPI reads never cross cores.
    esp_err_t result = nocti_fnirs_acquisition_start(xTaskGetCurrentTaskHandle());
    if (result != ESP_OK) {
        NOCTI_LOGE(TAG,
                   "ADS131 acquisition start for validation failed: %s",
                   esp_err_to_name(result));
        return result;
    }

    nocti_fnirs_frame_t frame;
    memset(&frame, 0, sizeof(frame));

    nocti_time_us_t first_ts_us = 0;
    nocti_time_us_t last_ts_us = 0;
    nocti_time_us_t previous_ts_us = 0;
    uint32_t previous_sequence = 0u;
    uint32_t sequence_gaps = 0u;
    uint32_t duplicate_sequences = 0u;
    uint32_t timestamp_gaps = 0u;
    uint32_t nonmonotonic_timestamps = 0u;
    uint32_t crc_bad = 0u;
    uint32_t min_period_us = UINT32_MAX;
    uint32_t max_period_us = 0u;

    for (uint32_t i = 0u; i < ADS131_RATE_VALIDATION_FRAMES; i++) {
        result = nocti_fnirs_read_frame(&frame);
        if (result != ESP_OK) {
            NOCTI_LOGE(TAG,
                       "ADS131 active-read validation stopped at frame %lu: %s",
                       (unsigned long)i,
                       esp_err_to_name(result));
            goto cleanup;
        }

        if (!frame.crc_ok) crc_bad++;

        if (i == 0u) {
            first_ts_us = frame.timestamp_us;
        } else {
            if (frame.timestamp_us <= previous_ts_us) {
                nonmonotonic_timestamps++;
            } else {
                const nocti_time_us_t period = frame.timestamp_us - previous_ts_us;
                const uint32_t period32 =
                    (period > (nocti_time_us_t)UINT32_MAX)
                        ? UINT32_MAX
                        : (uint32_t)period;
                if (period32 < min_period_us) min_period_us = period32;
                if (period32 > max_period_us) max_period_us = period32;

                // Pulse-format DRDY suppresses a subsequent pulse when the host
                // failed to service a prior conversion. Raw IRQ numbers may then
                // remain consecutive, so timestamp spacing is an independent
                // continuity check.
                uint32_t represented_periods =
                    (period32 + (ADS131_EXPECTED_PERIOD_US / 2u)) /
                    ADS131_EXPECTED_PERIOD_US;
                if (represented_periods == 0u) represented_periods = 1u;
                if (represented_periods > 1u) {
                    timestamp_gaps += represented_periods - 1u;
                }
            }

            const uint32_t seq_delta = frame.drdy_sequence - previous_sequence;
            if (seq_delta == 0u) {
                duplicate_sequences++;
            } else if (seq_delta > 1u) {
                sequence_gaps += seq_delta - 1u;
            }
        }

        previous_ts_us = frame.timestamp_us;
        previous_sequence = frame.drdy_sequence;
        last_ts_us = frame.timestamp_us;
    }

    if (last_ts_us <= first_ts_us) {
        NOCTI_LOGE(TAG, "ADS131 active-read validation has invalid timestamps");
        result = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }

    {
        const uint64_t elapsed_us = (uint64_t)(last_ts_us - first_ts_us);
        const uint32_t measured_sps = (uint32_t)(
            (((uint64_t)ADS131_RATE_VALIDATION_FRAMES - 1ull) * 1000000ull +
             (elapsed_us / 2ull)) / elapsed_us);
        const uint32_t minimum =
            (ADS131_SAMPLE_RATE_HZ * (100u - ADS131_RATE_TOLERANCE_PERCENT)) / 100u;
        const uint32_t maximum =
            (ADS131_SAMPLE_RATE_HZ * (100u + ADS131_RATE_TOLERANCE_PERCENT)) / 100u;
        const uint64_t inferred_clkin_hz =
            (uint64_t)measured_sps * 2ull * (uint64_t)ADS131_OSR;

        nocti_fnirs_stats_t driver_stats;
        memset(&driver_stats, 0, sizeof(driver_stats));
        nocti_fnirs_get_stats(&driver_stats);

        g_adc_validated_sps = measured_sps;
        g_adc_validation_min_period_us =
            (min_period_us == UINT32_MAX) ? 0u : min_period_us;
        g_adc_validation_max_period_us = max_period_us;

        NOCTI_LOGI(TAG,
                   "ADS131 active-read validation: core=%d frames=%u elapsed=%llu us "
                   "rate=%lu SPS required=%u..%u period=%lu..%lu us "
                   "seq_gap=%lu seq_dup=%lu ts_gap=%lu ts_nonmono=%lu "
                   "crc_bad=%lu event_overflow=%lu depth_max=%lu "
                   "irq_to_spi_max=%lu us spi_max=%lu us inferred_CLKIN=%llu Hz",
                   (int)xPortGetCoreID(),
                   (unsigned)ADS131_RATE_VALIDATION_FRAMES,
                   (unsigned long long)elapsed_us,
                   (unsigned long)measured_sps,
                   (unsigned)minimum,
                   (unsigned)maximum,
                   (unsigned long)((min_period_us == UINT32_MAX) ? 0u : min_period_us),
                   (unsigned long)max_period_us,
                   (unsigned long)sequence_gaps,
                   (unsigned long)duplicate_sequences,
                   (unsigned long)timestamp_gaps,
                   (unsigned long)nonmonotonic_timestamps,
                   (unsigned long)crc_bad,
                   (unsigned long)driver_stats.event_overflow_count,
                   (unsigned long)driver_stats.event_depth_max,
                   (unsigned long)driver_stats.max_irq_to_spi_start_us,
                   (unsigned long)driver_stats.max_spi_xfer_us,
                   (unsigned long long)inferred_clkin_hz);

        if (measured_sps < minimum || measured_sps > maximum ||
            sequence_gaps != 0u || duplicate_sequences != 0u ||
            timestamp_gaps != 0u || nonmonotonic_timestamps != 0u ||
            crc_bad != 0u || driver_stats.event_overflow_count != 0u) {
            NOCTI_LOGE(TAG,
                       "ADS131 validation failed; K target=%u/%u and K minimum=%u/%u "
                       "require a contiguous 8-kSPS stream",
                       (unsigned)FNIRS_K_ON_TARGET,
                       (unsigned)FNIRS_K_OFF_TARGET,
                       (unsigned)FNIRS_K_ON_MIN_VALID,
                       (unsigned)FNIRS_K_OFF_MIN_VALID);
            result = ESP_ERR_INVALID_STATE;
        }
    }

cleanup:
    // STOP disables DRDY capture and releases the validation task. The ADC keeps
    // converting, so production START always performs a fresh SYNC immediately
    // before its own event epoch.
    nocti_fnirs_acquisition_stop();
    return result;
}

typedef struct {
    SemaphoreHandle_t done;
    esp_err_t result;
    int8_t core_id;
} fnirs_validation_ctx_t;

static void fnirs_validation_task_entry(void *arg)
{
    fnirs_validation_ctx_t *ctx = (fnirs_validation_ctx_t *)arg;
    if (!ctx) {
        vTaskDelete(NULL);
        return;
    }

    ctx->core_id = (int8_t)xPortGetCoreID();
    ctx->result = validate_drdy_rate_once();
    (void)xSemaphoreGive(ctx->done);
    vTaskDelete(NULL);
}

static esp_err_t validate_drdy_rate_on_acq_core(void)
{
    fnirs_validation_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.result = ESP_FAIL;
    ctx.core_id = -1;
    ctx.done = xSemaphoreCreateBinary();
    if (!ctx.done) return ESP_ERR_NO_MEM;

    const BaseType_t created = xTaskCreatePinnedToCore(
        fnirs_validation_task_entry,
        "fnirs_validate",
        FNIRS_VALIDATION_STACK_BYTES,
        &ctx,
        FNIRS_VALIDATION_PRIORITY,
        NULL,
        FNIRS_ACQ_CORE);
    if (created != pdPASS) {
        vSemaphoreDelete(ctx.done);
        return ESP_ERR_NO_MEM;
    }

    (void)xSemaphoreTake(ctx.done, portMAX_DELAY);
    vSemaphoreDelete(ctx.done);
    if (ctx.core_id != FNIRS_ACQ_CORE) {
        NOCTI_LOGE(TAG,
                   "ADS131 validation ran on core=%d expected=%d",
                   (int)ctx.core_id,
                   FNIRS_ACQ_CORE);
        return ESP_ERR_INVALID_STATE;
    }
    return ctx.result;
}

// ============================================================
// Stream metadata
// ============================================================

static void push_fnirs_cfg_once(nocti_time_us_t now_us)
{
    nocti_payload_fnirs_cfg_v2_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.version = NOCTI_PAYLOAD_FNIRS_CFG_V2_VERSION;
    cfg.map_mode = (uint8_t)NOCTI_FNIRS_MAP_LEDID2_WL;
    cfg.frame_fs_hz = (uint8_t)FNIRS_FRAME_FS_HZ;
    cfg.slot_count = (uint8_t)FNIRS_SLOT_COUNT;
    cfg.k_on = (uint8_t)FNIRS_K_ON_TARGET;
    cfg.k_off = (uint8_t)FNIRS_K_OFF_TARGET;
    cfg.current_735_mA = (uint8_t)g_current_735_ma;
    cfg.current_850_mA = (uint8_t)g_current_850_ma;
    cfg.on_settle_us = (uint16_t)FNIRS_SETTLE_US;
    cfg.on_window_us = (uint16_t)FNIRS_TARGET_WINDOW_US;
    cfg.off_min_settle_us = (uint16_t)FNIRS_OFF_MIN_SETTLE_US;
    cfg.slot_period_us = (uint16_t)FNIRS_TICK_PERIOD_US;

    if (nocti_stream_push_fnirs_cfg_v2(&g_stream, now_us, &cfg) != ESP_OK) {
        g_stream_err_cnt++;
    }
}

static void push_test_config_event(nocti_time_us_t now_us)
{
    nocti_payload_event_v1_t event;
    memset(&event, 0, sizeof(event));

    event.version = NOCTI_PAYLOAD_EVENT_V1_VERSION;
    event.level = 0u;
    event.code = FNIRS_EVENT_CODE_TEST_CONFIG;
    event.arg = ((uint32_t)g_test_mode & 0xFFu) |
                (((uint32_t)g_selected_slot & 0xFFu) << 8u) |
                (((uint32_t)g_current_735_ma & 0xFFu) << 16u) |
                (((uint32_t)g_current_850_ma & 0xFFu) << 24u);

    if (nocti_stream_push_event_v1(&g_stream, now_us, &event) != ESP_OK) {
        g_stream_err_cnt++;
    }
}

// ============================================================
// Continuous ADS131M04 producer
// ============================================================

static inline uint32_t adc_wr_snapshot(void)
{
    return g_adc_wr;
}

static bool adc_wait_until_committed(nocti_time_us_t target_timestamp_us,
                                     uint32_t *out_write_snapshot)
{
    if (!out_write_snapshot) return false;

    const nocti_time_us_t deadline_us =
        nocti_timebase_now_us() + (nocti_time_us_t)FNIRS_ADC_COMMIT_WAIT_US;

    while (true) {
        const uint32_t snapshot = adc_wr_snapshot();
        if (snapshot > 0u) {
            const fnirs_adc_frame_t *latest =
                &g_adc_rb[(snapshot - 1u) % FNIRS_ADC_RB_SIZE];
            if (latest->ts_us >= target_timestamp_us) {
                *out_write_snapshot = snapshot;
                return true;
            }
        }

        if (!g_fnirs_adc_run || nocti_timebase_now_us() >= deadline_us) {
            *out_write_snapshot = snapshot;
            return false;
        }

        // The producer runs on the other core. Yielding here avoids pinning the
        // Wi-Fi/processing core while allowing the next committed DRDY frame to
        // become visible before the historical window is inspected.
        taskYIELD();
    }
}

static bool adc_history_covers_timestamp(uint32_t write_snapshot,
                                         nocti_time_us_t oldest_required_us)
{
    if (write_snapshot == 0u) return false;

    uint32_t available = write_snapshot;
    if (available > FNIRS_ADC_RB_SIZE) {
        available = FNIRS_ADC_RB_SIZE;
    }

    const uint32_t oldest_index = write_snapshot - available;
    const fnirs_adc_frame_t *oldest =
        &g_adc_rb[oldest_index % FNIRS_ADC_RB_SIZE];
    return oldest->ts_us <= oldest_required_us;
}

static void fnirs_adc_task_entry(void *arg)
{
    (void)arg;
    NOCTI_LOGI(TAG,
               "ADC task started core=%d priority=%u",
               (int)xPortGetCoreID(),
               (unsigned)uxTaskPriorityGet(NULL));

    while (true) {
        // The first DRDY interrupt after acquisition_start() wakes this task.
        // The same notification channel is also used by acquisition_stop() to
        // release a blocked event wait; the event ring remains the source of truth.
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (g_fnirs_adc_run) {
            nocti_fnirs_frame_t frame;
            const esp_err_t err = nocti_fnirs_read_frame(&frame);

            if (err != ESP_OK) {
                if (!g_fnirs_adc_run || err == ESP_ERR_INVALID_STATE) {
                    break;
                }
                g_adc_err_cnt++;
                diag_trace_push(FNIRS_TRACE_ADC_READ_ERROR,
                                (uint32_t)err,
                                g_adc_err_cnt);
                continue;
            }

            if (!g_fnirs_adc_run) break;
            if (!frame.crc_ok) g_adc_crc_bad_cnt++;

            uint32_t raw_irq_step = 1u;
            uint32_t time_step = 1u;
            bool timing_ok = true;

            if (g_adc_last_drdy_sequence != 0u) {
                raw_irq_step = frame.drdy_sequence - g_adc_last_drdy_sequence;
                if (raw_irq_step == 0u) {
                    g_adc_duplicate_irq_count++;
                    raw_irq_step = 1u;
                    timing_ok = false;
                }
            }

            if (g_adc_last_ts_us != 0) {
                if (frame.timestamp_us <= g_adc_last_ts_us) {
                    g_adc_nonmonotonic_ts_count++;
                    timing_ok = false;
                } else {
                    const nocti_time_us_t gap_us =
                        frame.timestamp_us - g_adc_last_ts_us;
                    const uint32_t gap32 =
                        (gap_us > (nocti_time_us_t)UINT32_MAX)
                            ? UINT32_MAX
                            : (uint32_t)gap_us;

                    if (gap32 > g_adc_max_gap_us) g_adc_max_gap_us = gap32;

                    time_step =
                        (gap32 + (ADS131_EXPECTED_PERIOD_US / 2u)) /
                        ADS131_EXPECTED_PERIOD_US;
                    if (time_step == 0u) time_step = 1u;
                    if (time_step > 1u) {
                        g_adc_period_gap_count += time_step - 1u;
                    }
                }
            }

            // Raw IRQ gaps expose event-ring/ISR losses. Timestamp gaps also
            // expose pulse-format DRDY suppression after an unserviced sample.
            const uint32_t logical_step =
                (raw_irq_step > time_step) ? raw_irq_step : time_step;
            if (g_adc_logical_sequence == 0u) {
                g_adc_logical_sequence = 1u;
            } else {
                g_adc_logical_sequence += logical_step;
            }

            if (logical_step > 1u) {
                g_adc_missed_drdy_count += logical_step - 1u;
                diag_trace_push(FNIRS_TRACE_ADC_PERIOD_GAP,
                                logical_step,
                                (raw_irq_step << 16) | (time_step & 0xFFFFu));
                if (logical_step > g_adc_max_sequence_gap) {
                    g_adc_max_sequence_gap = logical_step;
                }
            }

            g_adc_last_ts_us = frame.timestamp_us;
            g_adc_last_drdy_sequence = frame.drdy_sequence;

            const uint32_t write_index = g_adc_wr;
            fnirs_adc_frame_t *dst = &g_adc_rb[write_index % FNIRS_ADC_RB_SIZE];

            dst->ts_us = frame.timestamp_us;
            dst->drdy_sequence = g_adc_logical_sequence;
            dst->irq_sequence = frame.drdy_sequence;
            dst->ch[0] = frame.ch[0];
            dst->ch[1] = frame.ch[1];
            dst->ch[2] = frame.ch[2];
            dst->ch[3] = frame.ch[3];
            dst->crc_ok = frame.crc_ok;
            dst->timing_ok = timing_ok;

            // Commit marker: advance only after the entry is completely written.
            g_adc_wr = write_index + 1u;
            g_adc_ok_cnt++;
        }
    }
}

static uint8_t fnirs_collect_and_average(uint32_t write_snapshot,
                                         nocti_time_us_t t0_us,
                                         nocti_time_us_t t1_us,
                                         uint8_t k_fixed,
                                         int32_t out_avg[4],
                                         bool *out_any_crc_bad)
{
    if (!out_avg) {
        return 0u;
    }

    if (out_any_crc_bad) {
        *out_any_crc_bad = false;
    }

    int64_t accumulator[4] = {0, 0, 0, 0};
    uint8_t n_used = 0u;
    bool have_newer_sequence = false;
    uint32_t newer_sequence = 0u;

    uint32_t available = write_snapshot;

    if (available > FNIRS_ADC_RB_SIZE) {
        available = FNIRS_ADC_RB_SIZE;
    }
    if (available > FNIRS_ADC_SEARCH_BACK) {
        available = FNIRS_ADC_SEARCH_BACK;
    }

    // Scan newest to oldest. Limiting i < available prevents unsigned
    // underflow when a session has fewer than FNIRS_ADC_SEARCH_BACK samples.
    for (uint32_t i = 0u; i < available; i++) {
        const uint32_t index = write_snapshot - 1u - i;
        const fnirs_adc_frame_t *entry = &g_adc_rb[index % FNIRS_ADC_RB_SIZE];

        if (entry->ts_us < t0_us) {
            break;
        }
        if (entry->ts_us > t1_us) {
            continue;
        }

        // Precision rule: use only one contiguous block of the newest K
        // conversions. Never bridge across a CRC failure or a missing DRDY by
        // substituting an older sample, because that changes the effective
        // averaging time and can mix different optical states.
        if (!entry->crc_ok || !entry->timing_ok) {
            if (!entry->crc_ok && out_any_crc_bad) {
                *out_any_crc_bad = true;
            }
            break;
        }
        if (have_newer_sequence && (newer_sequence - entry->drdy_sequence) != 1u) {
            break;
        }

        accumulator[0] += (int64_t)entry->ch[0];
        accumulator[1] += (int64_t)entry->ch[1];
        accumulator[2] += (int64_t)entry->ch[2];
        accumulator[3] += (int64_t)entry->ch[3];

        n_used++;
        newer_sequence = entry->drdy_sequence;
        have_newer_sequence = true;
        if (n_used >= k_fixed) {
            break;
        }
    }

    if (n_used == 0u) {
        out_avg[0] = 0;
        out_avg[1] = 0;
        out_avg[2] = 0;
        out_avg[3] = 0;
        return 0u;
    }

    out_avg[0] = (int32_t)(accumulator[0] / (int64_t)n_used);
    out_avg[1] = (int32_t)(accumulator[1] / (int64_t)n_used);
    out_avg[2] = (int32_t)(accumulator[2] / (int64_t)n_used);
    out_avg[3] = (int32_t)(accumulator[3] / (int64_t)n_used);

    return n_used;
}

// ============================================================
// Frame finalization and scheduler callback
// ============================================================

static uint32_t duration_us_u32(nocti_time_us_t start_us,
                                nocti_time_us_t end_us)
{
    const nocti_time_us_t duration_us = end_us - start_us;
    if (duration_us <= 0) return 0u;
    if (duration_us > (nocti_time_us_t)UINT32_MAX) return UINT32_MAX;
    return (uint32_t)duration_us;
}

static void update_max_u32(volatile uint32_t *maximum, uint32_t value)
{
    if (maximum && value > *maximum) {
        *maximum = value;
    }
}

static void update_min_u32(volatile uint32_t *minimum, uint32_t value)
{
    if (minimum && value < *minimum) {
        *minimum = value;
    }
}

static void update_min_u8(volatile uint8_t *minimum, uint8_t value)
{
    if (minimum && value < *minimum) {
        *minimum = value;
    }
}

static void handle_fatal_led_driver_state(void)
{
    if (nocti_led_driver_is_ready()) {
        return;
    }

    g_led_fatal_fault = true;
    g_force_partial_frame = true;
    g_fnirs_adc_run = false;
    nocti_fnirs_acquisition_stop();
    g_run = false;

    // RESET# has already forced the PCA9956B outputs OFF. Stop the optical
    // timer immediately; the next START command will reconfigure the driver.
    nocti_scheduler_stop();
    NOCTI_LOGE(TAG,
               "fatal LED I2C fault: PCA9956B reset to OFF; acquisition stopped");
}

static void finalize_fnirs_frame(nocti_time_us_t timestamp_us,
                                 bool force_partial)
{
    if (!g_frame_open) return;

    const uint32_t expected_mask = (1u << FNIRS_SLOT_COUNT) - 1u;
    const uint32_t on_missing = expected_mask & ~g_fnirs_on_frame.valid_mask;
    const uint32_t off_missing = expected_mask & ~g_fnirs_off_frame.valid_mask;

    g_fnirs_on_frame.underk_mask |= on_missing;
    g_fnirs_off_frame.underk_mask |= off_missing;

    const bool pair_partial = force_partial ||
                              g_force_partial_frame ||
                              ((g_fnirs_on_frame.underk_mask & expected_mask) != 0u) ||
                              ((g_fnirs_off_frame.underk_mask & expected_mask) != 0u) ||
                              ((g_fnirs_on_frame.crc_mask & expected_mask) != 0u) ||
                              ((g_fnirs_off_frame.crc_mask & expected_mask) != 0u);
    if (pair_partial) {
        g_fnirs_on_frame.flags |= NOCTI_FNIRS_FRAME_F_PARTIAL;
        g_fnirs_off_frame.flags |= NOCTI_FNIRS_FRAME_F_PARTIAL;
        g_partial_frame_count++;
    }

    g_under_k_on_slot_count +=
        popcount32(g_fnirs_on_frame.underk_mask & expected_mask);
    g_under_k_off_slot_count +=
        popcount32(g_fnirs_off_frame.underk_mask & expected_mask);

    // Two records share the same frame_id. The host must pair them before
    // computing I_corrected = I_LED_ON - I_LED_OFF. A lost phase record causes
    // the complete pair to be discarded rather than silently using stale data.
    if (nocti_stream_push_fnirs_frame_v1(
            &g_stream, timestamp_us, &g_fnirs_on_frame) != ESP_OK) {
        g_stream_err_cnt++;
    }
    if (nocti_stream_push_fnirs_frame_v1(
            &g_stream, timestamp_us, &g_fnirs_off_frame) != ESP_OK) {
        g_stream_err_cnt++;
    }

    (void)nocti_stream_maybe_push_time64(&g_stream, timestamp_us);

    g_frame_count++;
    g_fnirs_frame_id++;
    fnirs_frame_reset();
    g_frame_open = false;
    g_force_partial_frame = false;
}

static void synchronize_accumulator_frame(uint32_t frame_index,
                                          nocti_time_us_t timestamp_us)
{
    if (!g_scheduler_frame_seen) {
        g_scheduler_frame_seen = true;
        g_scheduler_frame_index = frame_index;
        return;
    }

    if (frame_index != g_scheduler_frame_index) {
        // A missing queue job or aborted slot must never cause samples from two
        // optical frames to be mixed. Close the previous frame as partial.
        finalize_fnirs_frame(timestamp_us, true);
        g_scheduler_frame_index = frame_index;
    }
}

static bool enqueue_slot_job(fnirs_slot_job_t *job)
{
    if (!job || !job->valid) return true;

    if (!job->complete) {
        job->aborted = true;
        job->force_partial = true;
        job->complete = true;
    }

    // Queueing is non-blocking because this function runs in the timing-critical
    // scheduler callback. The processing task waits until the ADC producer has
    // committed through the optical endpoint before taking its ring snapshot.
    bool queued = false;
    if (g_fnirs_process_queue != NULL) {
        queued = xQueueSend(g_fnirs_process_queue, job, 0) == pdTRUE;
    }

    if (!queued) {
        g_process_queue_drop_count++;
    }

    memset(job, 0, sizeof(*job));
    return queued;
}

static void process_slot_job(const fnirs_slot_job_t *job)
{
    if (!job || !job->valid || !job->complete) return;
    if (job->generation != g_acq_generation) return;

    if (g_fnirs_process_mutex != NULL) {
        (void)xSemaphoreTake(g_fnirs_process_mutex, portMAX_DELAY);
    }

    // Re-check after waiting for the mutex in case a rapid STOP/START advanced
    // the acquisition generation while this job was queued.
    if (job->generation != g_acq_generation) {
        if (g_fnirs_process_mutex != NULL) {
            (void)xSemaphoreGive(g_fnirs_process_mutex);
        }
        return;
    }

    synchronize_accumulator_frame(job->frame_index, job->timestamp_us);
    g_frame_open = true;

    const uint32_t slot_bit = (1u << job->slot_index);

    if (job->aborted) {
        g_force_partial_frame = true;
        g_fnirs_on_frame.underk_mask |= slot_bit;
        g_fnirs_off_frame.underk_mask |= slot_bit;
    } else {
        int32_t off_average[4] = {0, 0, 0, 0};
        int32_t on_average[4] = {0, 0, 0, 0};
        bool off_crc_bad = false;
        bool on_crc_bad = false;
        uint8_t off_n_used = 0u;
        uint8_t on_n_used = 0u;

        // Wait until the producer has committed every conversion whose timestamp
        // can belong to this pair. This avoids taking a snapshot immediately after
        // the LED transition while the corresponding DRDY frame is still in the
        // ADS131 FIFO or in the higher-priority ADC task.
        const nocti_time_us_t latest_endpoint_us =
            (job->off_t1_us > job->on_t1_us) ? job->off_t1_us : job->on_t1_us;
        const nocti_time_us_t oldest_required_us =
            (job->off_t0_us < job->on_t0_us) ? job->off_t0_us : job->on_t0_us;
        uint32_t processing_snapshot = 0u;
        const bool adc_committed =
            adc_wait_until_committed(latest_endpoint_us, &processing_snapshot);
        if (!adc_committed) {
            g_process_adc_wait_timeout_count++;
        }

        // If processing fell behind far enough for the oldest required sample to
        // be overwritten, fail closed instead of reading a newer optical state.
        const bool history_available =
            adc_committed &&
            adc_history_covers_timestamp(processing_snapshot, oldest_required_us);

        if (history_available && job->off_t1_us > job->off_t0_us) {
            off_n_used = fnirs_collect_and_average(
                processing_snapshot,
                job->off_t0_us,
                job->off_t1_us,
                (uint8_t)FNIRS_K_OFF_TARGET,
                off_average,
                &off_crc_bad);
        }

        if (history_available && job->on_t1_us > job->on_t0_us) {
            on_n_used = fnirs_collect_and_average(
                processing_snapshot,
                job->on_t0_us,
                job->on_t1_us,
                (uint8_t)FNIRS_K_ON_TARGET,
                on_average,
                &on_crc_bad);
        }

        update_min_u8(&g_off_min_n_used, off_n_used);
        update_min_u8(&g_on_min_n_used, on_n_used);

        if (off_n_used >= FNIRS_K_OFF_MIN_VALID &&
            off_n_used < FNIRS_K_OFF_TARGET) {
            g_subtarget_off_slot_count++;
        }
        if (on_n_used >= FNIRS_K_ON_MIN_VALID &&
            on_n_used < FNIRS_K_ON_TARGET) {
            g_subtarget_on_slot_count++;
        }

        fnirs_store_phase_slot(&g_fnirs_off_frame,
                               job->slot_index,
                               off_n_used,
                               off_average,
                               off_crc_bad,
                               (uint8_t)FNIRS_K_OFF_MIN_VALID);
        fnirs_store_phase_slot(&g_fnirs_on_frame,
                               job->slot_index,
                               on_n_used,
                               on_average,
                               on_crc_bad,
                               (uint8_t)FNIRS_K_ON_MIN_VALID);

        if (job->force_partial || !history_available) {
            g_force_partial_frame = true;
        }
    }

    if (job->scheduler_slot_index == (FNIRS_SLOT_COUNT - 1u)) {
        finalize_fnirs_frame(job->timestamp_us, false);
    }

    g_process_job_count++;

    if (g_fnirs_process_mutex != NULL) {
        (void)xSemaphoreGive(g_fnirs_process_mutex);
    }
}

static void fnirs_process_task_entry(void *arg)
{
    (void)arg;
    g_process_task_core = (int8_t)xPortGetCoreID();
    NOCTI_LOGI(TAG,
               "processing task started core=%d priority=%u",
               (int)xPortGetCoreID(),
               (unsigned)uxTaskPriorityGet(NULL));

    fnirs_slot_job_t job;
    while (true) {
        if (xQueueReceive(g_fnirs_process_queue, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        g_process_busy = true;
        process_slot_job(&job);
        g_process_busy = false;
    }
}

static esp_err_t scheduler_callback(const nocti_sched_event_t *event,
                                    void *user)
{
    (void)user;

    if (!event || !g_run) return ESP_OK;

    if (event->phase == NOCTI_EV_DARK_TICK) {
        (void)nocti_stream_maybe_push_time64(&g_stream, event->t_event_us);
        return ESP_OK;
    }

    const uint8_t slot_index = slot_index_from_event(event);
    if (slot_index >= FNIRS_SLOT_COUNT) return ESP_ERR_INVALID_ARG;

    const bool led_active = slot_led_is_active(slot_index);

    if (event->phase == NOCTI_EV_SLOT_ABORT) {
        g_slot_abort_count++;
        diag_trace_push(FNIRS_TRACE_SCHED_ABORT,
                        (uint32_t)event->abort_reason,
                        event->slot_index);

        // A late-dispatch abort occurs before SLOT_BEGIN, so this slot never
        // changed the LED state. Avoiding a redundant all-off I2C transaction
        // prevents one rejected slot from consuming several milliseconds and
        // cascading into the next slot. Abort reasons after BEGIN still force a
        // physical all-off because the LED state may be ON or unknown.
        esp_err_t off_err = ESP_OK;
        if (event->abort_reason != NOCTI_ABORT_LATE_DISPATCH) {
            const nocti_time_us_t off_start_us = nocti_timebase_now_us();
            off_err = nocti_led_driver_all_off();
            const nocti_time_us_t off_done_us = nocti_timebase_now_us();
            g_led_off_count++;
            update_max_u32(&g_led_off_max_us,
                           duration_us_u32(off_start_us, off_done_us));

            if (off_err == ESP_OK) {
                g_led_off_ready_us = off_done_us;
            } else {
                g_led_err_cnt++;
                g_led_off_ready_us = 0;
                handle_fatal_led_driver_state();
            }
        }

        // If BEGIN already created the current job, convert it to an abort.
        // Otherwise first release the completed previous slot, then enqueue a
        // synthetic abort record for the current program slot.
        if (g_pending_slot_job.valid &&
            g_pending_slot_job.frame_index == event->frame_index &&
            g_pending_slot_job.scheduler_slot_index == event->slot_index) {
            g_pending_slot_job.aborted = true;
            g_pending_slot_job.force_partial = true;
            g_pending_slot_job.complete = true;
            g_pending_slot_job.timestamp_us = event->t_event_us;
            (void)enqueue_slot_job(&g_pending_slot_job);
        } else {
            (void)enqueue_slot_job(&g_pending_slot_job);

            fnirs_slot_job_t abort_job;
            memset(&abort_job, 0, sizeof(abort_job));
            abort_job.generation = g_acq_generation;
            abort_job.frame_index = event->frame_index;
            abort_job.scheduler_slot_index = event->slot_index;
            abort_job.slot_index = slot_index;
            abort_job.aborted = true;
            abort_job.force_partial = true;
            abort_job.valid = true;
            abort_job.complete = true;
            abort_job.timestamp_us = event->t_event_us;
            (void)enqueue_slot_job(&abort_job);
        }

        return off_err;
    }

    if (event->phase == NOCTI_EV_SLOT_BEGIN) {
        // The physical ON edge is always the first nontrivial work in a slot.
        // Averaging and stream assembly have been moved to a lower-priority task.
        const nocti_time_us_t on_command_start_us = nocti_timebase_now_us();

        esp_err_t err = ESP_OK;
        nocti_time_us_t on_ready_us = on_command_start_us;
        if (led_active) {
            nocti_time_us_t callback_edge_us = 0;
            err = nocti_led_driver_slot_begin_timed(
                &event->slot, &callback_edge_us);
            if (err == ESP_OK &&
                (callback_edge_us < on_command_start_us ||
                 duration_us_u32(on_command_start_us, callback_edge_us) >
                     FNIRS_LED_EDGE_TIMESTAMP_MAX_US)) {
                // A bus completion in a different epoch, in the past, or beyond
                // the runtime timing budget is not a usable physical LED edge.
                err = ESP_ERR_INVALID_STATE;
            }
            on_ready_us = err == ESP_OK
                ? callback_edge_us
                : nocti_timebase_now_us();

            g_led_on_count++;
            update_max_u32(&g_led_on_max_us,
                           duration_us_u32(on_command_start_us, on_ready_us));
        }

        if (err != ESP_OK) {
            g_led_err_cnt++;
            g_led_off_ready_us = 0;
            handle_fatal_led_driver_state();
            return err;
        }

        if (!g_run) {
            const esp_err_t off_err = nocti_led_driver_all_off();
            if (off_err != ESP_OK) {
                g_led_err_cnt++;
                handle_fatal_led_driver_state();
                return off_err;
            }
            g_led_off_ready_us = nocti_timebase_now_us();
            return ESP_ERR_INVALID_STATE;
        }

        // The previous slot ended at least one half-slot earlier, so all of its
        // ADC frames are now committed. Queue it only after the current ON edge,
        // ensuring processing can never delay that edge.
        (void)enqueue_slot_job(&g_pending_slot_job);

        memset(&g_pending_slot_job, 0, sizeof(g_pending_slot_job));
        g_pending_slot_job.generation = g_acq_generation;
        g_pending_slot_job.frame_index = event->frame_index;
        g_pending_slot_job.scheduler_slot_index = event->slot_index;
        g_pending_slot_job.slot_index = slot_index;
        g_pending_slot_job.force_partial = event->missed_ticks_before > 0u;
        g_pending_slot_job.valid = true;
        g_pending_slot_job.complete = false;
        g_pending_slot_job.timestamp_us = event->t_event_us;

        if (g_led_off_ready_us > 0) {
            // MODE2.OCH is programmed to 0 and verified during LED-driver init.
            // Therefore the old OFF state remains physically active throughout
            // the LED-ON I2C transaction and changes only at its STOP condition.
            // Use on_ready_us (post-STOP), not on_command_start_us, as the real
            // end of the LED-off intensity interval.
            const uint32_t total_dark_us =
                duration_us_u32(g_led_off_ready_us, on_ready_us);
            update_min_u32(&g_off_dark_min_us, total_dark_us);

            g_pending_slot_job.off_t0_us =
                g_led_off_ready_us + (nocti_time_us_t)FNIRS_OFF_MIN_SETTLE_US;
            g_pending_slot_job.off_t1_us = on_ready_us;

            if (g_pending_slot_job.off_t1_us > g_pending_slot_job.off_t0_us) {
                update_min_u32(&g_off_stable_min_us,
                               duration_us_u32(g_pending_slot_job.off_t0_us,
                                               g_pending_slot_job.off_t1_us));
            }
        }

        g_pending_slot_job.on_t0_us =
            on_ready_us + (nocti_time_us_t)FNIRS_SETTLE_US;
        return ESP_OK;
    }

    if (event->phase == NOCTI_EV_SLOT_SAMPLE) {
        return ESP_OK;
    }

    if (event->phase != NOCTI_EV_SLOT_END) return ESP_OK;

    // Issue LED-OFF at the fixed command deadline. With MODE2.OCH=0 the LED
    // remains physically ON until the PCA9956B observes the transaction STOP.
    const nocti_time_us_t off_command_start_us = nocti_timebase_now_us();
    nocti_time_us_t physical_off_edge_us = off_command_start_us;

    esp_err_t led_err = ESP_OK;
    if (led_active) {
        nocti_time_us_t callback_edge_us = 0;
        led_err = nocti_led_driver_slot_end_timed(
            &event->slot, &callback_edge_us);
        if (led_err == ESP_OK &&
            (callback_edge_us < off_command_start_us ||
             duration_us_u32(off_command_start_us, callback_edge_us) >
                 FNIRS_LED_EDGE_TIMESTAMP_MAX_US)) {
            led_err = ESP_ERR_INVALID_STATE;
        }
        const nocti_time_us_t off_done_us = led_err == ESP_OK
            ? callback_edge_us
            : nocti_timebase_now_us();

        g_led_off_count++;
        update_max_u32(&g_led_off_max_us,
                       duration_us_u32(off_command_start_us, off_done_us));

        if (led_err == ESP_OK) {
            physical_off_edge_us = off_done_us;
            g_led_off_ready_us = off_done_us;
        } else {
            g_led_err_cnt++;
            g_led_off_ready_us = 0;
            handle_fatal_led_driver_state();
        }
    }

    if (g_pending_slot_job.valid &&
        g_pending_slot_job.frame_index == event->frame_index &&
        g_pending_slot_job.scheduler_slot_index == event->slot_index) {
        // End the LED-ON interval at the conservative post-STOP completion timestamp,
        // not at I2C START. Samples acquired while the OFF command is still
        // shifting are still physically illuminated and are valid ON samples.
        g_pending_slot_job.on_t1_us = physical_off_edge_us;
        g_pending_slot_job.timestamp_us = event->t_event_us;
        g_pending_slot_job.complete = true;

        const nocti_time_us_t on_ready_us =
            g_pending_slot_job.on_t0_us - (nocti_time_us_t)FNIRS_SETTLE_US;
        update_min_u32(&g_on_light_min_us,
                       duration_us_u32(on_ready_us, physical_off_edge_us));
        if (g_pending_slot_job.on_t1_us > g_pending_slot_job.on_t0_us) {
            update_min_u32(&g_on_stable_min_us,
                           duration_us_u32(g_pending_slot_job.on_t0_us,
                                           g_pending_slot_job.on_t1_us));
        }
    } else {
        // A missing BEGIN is represented explicitly as an aborted slot.
        fnirs_slot_job_t abort_job;
        memset(&abort_job, 0, sizeof(abort_job));
        abort_job.generation = g_acq_generation;
        abort_job.frame_index = event->frame_index;
        abort_job.scheduler_slot_index = event->slot_index;
        abort_job.slot_index = slot_index;
        abort_job.aborted = true;
        abort_job.force_partial = true;
        abort_job.valid = true;
        abort_job.complete = true;
        abort_job.timestamp_us = event->t_event_us;
        (void)enqueue_slot_job(&abort_job);
    }

    return led_err;
}

typedef enum {
    FNIRS_LED_CONTROL_INIT = 0,
    FNIRS_LED_CONTROL_RECOVER,
} fnirs_led_control_op_t;

typedef struct {
    SemaphoreHandle_t done;
    fnirs_led_control_op_t operation;
    const nocti_led_driver_cfg_t *cfg;
    esp_err_t result;
    int8_t core_id;
} fnirs_led_control_ctx_t;

static void fnirs_led_control_task_entry(void *arg)
{
    fnirs_led_control_ctx_t *ctx = (fnirs_led_control_ctx_t *)arg;
    if (!ctx) {
        vTaskDelete(NULL);
        return;
    }

    ctx->core_id = (int8_t)xPortGetCoreID();
    if (ctx->operation == FNIRS_LED_CONTROL_INIT) {
        ctx->result = nocti_led_driver_init(ctx->cfg);
    } else {
        ctx->result = nocti_led_driver_recover();
    }

    (void)xSemaphoreGive(ctx->done);
    vTaskDelete(NULL);
}

static esp_err_t run_led_control_on_acq_core(
    fnirs_led_control_op_t operation,
    const nocti_led_driver_cfg_t *cfg)
{
    if (operation == FNIRS_LED_CONTROL_INIT && !cfg) {
        return ESP_ERR_INVALID_ARG;
    }

    fnirs_led_control_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.operation = operation;
    ctx.cfg = cfg;
    ctx.result = ESP_FAIL;
    ctx.core_id = -1;
    ctx.done = xSemaphoreCreateBinary();
    if (!ctx.done) return ESP_ERR_NO_MEM;

    const BaseType_t created = xTaskCreatePinnedToCore(
        fnirs_led_control_task_entry,
        "fnirs_led_ctl",
        FNIRS_LED_CONTROL_STACK_BYTES,
        &ctx,
        FNIRS_VALIDATION_PRIORITY,
        NULL,
        FNIRS_ACQ_CORE);
    if (created != pdPASS) {
        vSemaphoreDelete(ctx.done);
        return ESP_ERR_NO_MEM;
    }

    (void)xSemaphoreTake(ctx.done, portMAX_DELAY);
    vSemaphoreDelete(ctx.done);

    NOCTI_LOGI(TAG,
               "LED control operation=%s completed core=%d result=%s",
               operation == FNIRS_LED_CONTROL_INIT ? "init" : "recover",
               (int)ctx.core_id,
               esp_err_to_name(ctx.result));
    if (ctx.core_id != FNIRS_ACQ_CORE) {
        return ESP_ERR_INVALID_STATE;
    }
    return ctx.result;
}

static bool core_plan_matches_runtime(void)
{
    nocti_timebase_status_t timebase_status;
    nocti_fnirs_stats_t fnirs_stats;
    nocti_led_driver_stats_t led_stats;
    nocti_scheduler_stats_t scheduler_stats;
    memset(&timebase_status, 0, sizeof(timebase_status));
    memset(&fnirs_stats, 0, sizeof(fnirs_stats));
    memset(&led_stats, 0, sizeof(led_stats));
    memset(&scheduler_stats, 0, sizeof(scheduler_stats));
    nocti_timebase_get_status(&timebase_status);
    nocti_fnirs_get_stats(&fnirs_stats);
    nocti_led_driver_get_stats(&led_stats);
    nocti_scheduler_get_stats(&scheduler_stats);

    return g_platform_init_core == FNIRS_ACQ_CORE &&
           timebase_status.init_core_id == FNIRS_ACQ_CORE &&
           (timebase_status.alarm_isr_core_id < 0 ||
            timebase_status.alarm_isr_core_id == FNIRS_ACQ_CORE) &&
           fnirs_stats.isr_core_id == FNIRS_ACQ_CORE &&
           fnirs_stats.consumer_core_id == FNIRS_ACQ_CORE &&
           led_stats.i2c_init_core_id == FNIRS_ACQ_CORE &&
           (led_stats.i2c_callback_core_id < 0 ||
            led_stats.i2c_callback_core_id == FNIRS_ACQ_CORE) &&
           scheduler_stats.task_core_id == FNIRS_ACQ_CORE &&
           g_process_task_core == FNIRS_PROCESS_CORE;
}

// ============================================================
// Acquisition start and stop
// ============================================================

static void stop_fnirs_acquisition(void)
{
    const bool was_active = g_run ||
                            g_fnirs_adc_run ||
                            nocti_scheduler_is_running() ||
                            g_led_fatal_fault ||
                            g_pending_slot_job.valid;

    if (was_active) {
        diag_trace_push(FNIRS_TRACE_STOP, g_session_id, g_frame_count);
    }

    // Gate the scheduler callback before stopping the timer.
    g_run = false;
    nocti_scheduler_stop();

    // nocti_scheduler_stop() disables future ticks but does not join a callback
    // already executing on the acquisition core. One full slot guarantees any
    // in-flight LED I2C transaction has returned before the control task issues
    // the final all-off command or touches the pending-job descriptor.
    if (was_active) {
        vTaskDelay(pdMS_TO_TICKS(6u));
    }

    if (nocti_led_driver_is_ready()) {
        const nocti_time_us_t off_start_us = nocti_timebase_now_us();
        const esp_err_t off_err = nocti_led_driver_all_off();
        const nocti_time_us_t off_done_us = nocti_timebase_now_us();
        if (off_err == ESP_OK) {
            g_led_off_ready_us = off_done_us;
            g_led_off_count++;
            update_max_u32(&g_led_off_max_us,
                           duration_us_u32(off_start_us, off_done_us));
        }
    }

    if (was_active && g_fnirs_adc_run) {
        // Allow any DRDY that occurred just before STOP to be transferred from
        // the ADS131 FIFO into the software ring before snapshotting the final
        // pending slot. An incomplete optical slot is enqueued as an abort.
        vTaskDelay(pdMS_TO_TICKS(2u));
        (void)enqueue_slot_job(&g_pending_slot_job);

        // Keep the ADC producer alive while the lower-priority processor drains
        // historical windows. Bound the wait so STOP can never hang forever.
        for (uint32_t waited_ms = 0u;
             waited_ms < FNIRS_PROCESS_DRAIN_TIMEOUT_MS;
             waited_ms++) {
            const UBaseType_t queued =
                (g_fnirs_process_queue != NULL)
                    ? uxQueueMessagesWaiting(g_fnirs_process_queue)
                    : 0u;
            if (queued == 0u && !g_process_busy) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1u));
        }
    } else {
        memset(&g_pending_slot_job, 0, sizeof(g_pending_slot_job));
    }

    g_fnirs_adc_run = false;
    nocti_fnirs_acquisition_stop();

    // acquisition_stop() disables DRDY and directly wakes the event consumer;
    // one RTOS tick is enough for the ADC task to leave its inner loop.
    if (was_active) {
        vTaskDelay(pdMS_TO_TICKS(1u));
    }
}

static esp_err_t start_fnirs_acquisition(void)
{
    // A repeated START creates a clean new acquisition boundary.
    if (g_run ||
        g_fnirs_adc_run ||
        nocti_scheduler_is_running() ||
        g_led_fatal_fault ||
        g_pending_slot_job.valid ||
        (g_fnirs_process_queue != NULL &&
         uxQueueMessagesWaiting(g_fnirs_process_queue) != 0u)) {
        stop_fnirs_acquisition();
    }

    esp_err_t err = ESP_OK;
    if (!nocti_led_driver_is_ready()) {
        err = run_led_control_on_acq_core(FNIRS_LED_CONTROL_RECOVER, NULL);
        if (err != ESP_OK) {
            return err;
        }
    }

    reset_runtime_counters();
    diag_trace_push(FNIRS_TRACE_START, g_session_id, (uint32_t)g_test_mode);

    // Jobs from a previous START can never be accepted by the processor after
    // the generation changes, even if a rapid host reconnect caused overlap.
    g_acq_generation++;
    if (g_acq_generation == 0u) g_acq_generation = 1u;

    if (g_fnirs_process_queue != NULL) {
        (void)xQueueReset(g_fnirs_process_queue);
    }
    memset(&g_pending_slot_job, 0, sizeof(g_pending_slot_job));

    if (g_fnirs_process_mutex != NULL) {
        (void)xSemaphoreTake(g_fnirs_process_mutex, portMAX_DELAY);
    }
    g_fnirs_frame_id = 0u;
    fnirs_frame_reset();
    g_frame_open = false;
    g_force_partial_frame = false;
    g_scheduler_frame_seen = false;
    g_scheduler_frame_index = 0u;
    if (g_fnirs_process_mutex != NULL) {
        (void)xSemaphoreGive(g_fnirs_process_mutex);
    }

    g_led_off_ready_us = 0;

    // Reset the write index only after the producer has stopped. Old entries
    // are ignored because every processing job carries the new generation.
    g_adc_wr = 0u;

    if (!core_plan_matches_runtime()) {
        NOCTI_LOGE(TAG,
                   "core plan validation failed; inspect STATUS core fields before START");
        return ESP_ERR_INVALID_STATE;
    }

    err = nocti_stream_start_session_now(&g_stream, g_session_id);
    if (err != ESP_OK) {
        return err;
    }

    const nocti_time_us_t now_us = nocti_timebase_now_us();
    push_fnirs_cfg_once(now_us);
    push_test_config_event(now_us);

    err = nocti_led_driver_all_off();
    if (err != ESP_OK) {
        handle_fatal_led_driver_state();
        return err;
    }
    g_led_off_ready_us = nocti_timebase_now_us();

    // Bind DRDY capture permanently to the ADC task, clear the software event
    // ring, and issue a non-reset SYNC pulse to clear the ADS131 two-deep FIFO.
    g_fnirs_adc_run = true;
    err = nocti_fnirs_acquisition_start(g_fnirs_adc_task);
    if (err != ESP_OK) {
        g_fnirs_adc_run = false;
        NOCTI_LOGE(TAG, "ADS131 event engine START failed: %s",
                   esp_err_to_name(err));
        return err;
    }

    // Give the continuous producer time to populate a fully settled OFF window
    // before the first absolute optical tick.
    vTaskDelay(pdMS_TO_TICKS(FNIRS_START_PREFILL_MS));

    g_run = true;
    if (!nocti_scheduler_start()) {
        g_run = false;
        g_fnirs_adc_run = false;
        nocti_fnirs_acquisition_stop();
        (void)nocti_led_driver_all_off();
        vTaskDelay(pdMS_TO_TICKS(1u));
        return ESP_FAIL;
    }

    return ESP_OK;
}

// ============================================================
// WiFi transport state helpers
// ============================================================

static void wifi_set_target(nocti_wifi_t *wifi, const nocti_wifi_peer_t *peer)
{
    if (!wifi || !peer || !wifi->mutex) {
        return;
    }

    SemaphoreHandle_t mutex = (SemaphoreHandle_t)wifi->mutex;
    (void)xSemaphoreTake(mutex, portMAX_DELAY);
    wifi->target = *peer;
    wifi->has_target = true;
    (void)xSemaphoreGive(mutex);
}

static uint32_t wifi_prepare_new_session(nocti_wifi_t *wifi,
                                         const nocti_wifi_peer_t *peer)
{
    if (!wifi || !peer || !wifi->mutex) {
        return 0u;
    }

    SemaphoreHandle_t mutex = (SemaphoreHandle_t)wifi->mutex;
    (void)xSemaphoreTake(mutex, portMAX_DELAY);

    wifi->target = *peer;
    wifi->has_target = true;
    wifi->streaming_enabled = false;
    wifi->session_id += 1u;
    wifi->stream_seq = 0u;

    const uint32_t session_id = wifi->session_id;
    (void)xSemaphoreGive(mutex);
    return session_id;
}

static void wifi_set_streaming_enabled(nocti_wifi_t *wifi, bool enabled)
{
    if (!wifi || !wifi->mutex) {
        return;
    }

    SemaphoreHandle_t mutex = (SemaphoreHandle_t)wifi->mutex;
    (void)xSemaphoreTake(mutex, portMAX_DELAY);
    wifi->streaming_enabled = enabled;
    (void)xSemaphoreGive(mutex);
}

// ============================================================
// UDP command handling
// ============================================================

static void send_status(nocti_wifi_t *wifi, const nocti_wifi_peer_t *peer)
{
    nocti_scheduler_stats_t scheduler_stats;
    nocti_led_driver_stats_t led_stats;
    nocti_timebase_status_t timebase_status;
    nocti_fnirs_stats_t fnirs_driver_stats;
    memset(&scheduler_stats, 0, sizeof(scheduler_stats));
    memset(&led_stats, 0, sizeof(led_stats));
    memset(&timebase_status, 0, sizeof(timebase_status));
    memset(&fnirs_driver_stats, 0, sizeof(fnirs_driver_stats));
    nocti_scheduler_get_stats(&scheduler_stats);
    nocti_led_driver_get_stats(&led_stats);
    nocti_timebase_get_status(&timebase_status);
    nocti_fnirs_get_stats(&fnirs_driver_stats);
    const UBaseType_t process_queued =
        (g_fnirs_process_queue != NULL)
            ? uxQueueMessagesWaiting(g_fnirs_process_queue)
            : 0u;

    send_ctrlf(
        wifi,
        peer,
        "FNIRS STATUS running=%u mode=%s slot=%u current735=%u current850=%u "
        "frames=%lu partial=%lu underk_on=%lu underk_off=%lu "
        "subtarget_on=%lu subtarget_off=%lu abort_slots=%lu "
        "adc_ok=%lu adc_err=%lu adc_max_gap_us=%lu adc_missed=%lu "
        "adc_seq_gap_max=%lu crc_bad=%lu adc_irq_dup=%lu "
        "adc_ts_nonmono=%lu adc_period_gap=%lu "
        "drdy_capture=%u drdy_irq=%lu drdy_push=%lu drdy_pop=%lu "
        "drdy_overflow=%lu drdy_wait_timeout=%lu drdy_sync=%lu "
        "drdy_depth_max=%lu fifo_risk=%lu drdy_timing_fault=%lu "
        "spi_recovery=%lu fifo_recovery=%lu "
        "drdy_isr_core=%d adc_consumer_core=%d "
        "irq_to_spi_max_us=%lu spi_xfer_max_us=%lu frame_service_max_us=%lu "
        "led_err=%lu led_on_max_us=%lu led_off_max_us=%lu "
        "led_i2c_writes=%lu led_i2c_errors=%lu led_fast_writes=%lu "
        "led_fast_errors=%lu led_fast_ready=%u led_i2c_init_core=%d "
        "led_i2c_callback_core=%d led_hw_resets=%lu "
        "led_state_known=%u led_ready=%u led_fatal=%u "
        "tb_irq=%llu tb_ticks=%llu tb_catchup=%llu tb_late_max_us=%u "
        "tb_read_err=%u tb_rearm_err=%u tb_init_core=%d tb_isr_core=%d "
        "core_plan_ok=%u platform_core=%d process_core=%d sched_core=%d "
        "sched_missed=%lu sched_late=%lu sched_late_max_us=%lu "
        "sched_abort=%lu sched_abort_pre=%lu sched_abort_begin=%lu "
        "sched_abort_post=%lu sched_overrun=%lu sched_cb_max_us=%lu "
        "stream_err=%lu drops=%lu "
        "adc_sps=%u adc_sps_validated=%lu adc_validation_period_us=%lu..%lu "
        "k_on=%u k_off=%u k_min_on=%u k_min_off=%u "
        "min_n_on=%u min_n_off=%u target_window_us=%u min_window_us=%u "
        "on_deadline_us=%u off_budget_us=%u settle_us=%u "
        "on_light_min_us=%lu on_stable_min_us=%lu "
        "off_dark_min_us=%lu off_stable_min_us=%lu "
        "process_jobs=%lu process_q=%u process_drop=%lu process_wait_timeout=%lu "
        "process_busy=%u i2c_hz=%u serial_quiet=%u health_serial=0 "
        "health_suppressed=%lu trace_events=%lu trace_capacity=%u "
        "protocol=paired_v2_4_8k session=%lu\n",
        g_run ? 1u : 0u,
        test_mode_name(g_test_mode),
        (unsigned)g_selected_slot,
        (unsigned)g_current_735_ma,
        (unsigned)g_current_850_ma,
        (unsigned long)g_frame_count,
        (unsigned long)g_partial_frame_count,
        (unsigned long)g_under_k_on_slot_count,
        (unsigned long)g_under_k_off_slot_count,
        (unsigned long)g_subtarget_on_slot_count,
        (unsigned long)g_subtarget_off_slot_count,
        (unsigned long)g_slot_abort_count,
        (unsigned long)g_adc_ok_cnt,
        (unsigned long)g_adc_err_cnt,
        (unsigned long)g_adc_max_gap_us,
        (unsigned long)g_adc_missed_drdy_count,
        (unsigned long)g_adc_max_sequence_gap,
        (unsigned long)g_adc_crc_bad_cnt,
        (unsigned long)g_adc_duplicate_irq_count,
        (unsigned long)g_adc_nonmonotonic_ts_count,
        (unsigned long)g_adc_period_gap_count,
        fnirs_driver_stats.capture_enabled ? 1u : 0u,
        (unsigned long)fnirs_driver_stats.irq_count,
        (unsigned long)fnirs_driver_stats.event_push_count,
        (unsigned long)fnirs_driver_stats.event_pop_count,
        (unsigned long)fnirs_driver_stats.event_overflow_count,
        (unsigned long)fnirs_driver_stats.wait_timeout_count,
        (unsigned long)fnirs_driver_stats.sync_count,
        (unsigned long)fnirs_driver_stats.event_depth_max,
        (unsigned long)fnirs_driver_stats.fifo_risk_count,
        (unsigned long)fnirs_driver_stats.timing_fault_count,
        (unsigned long)fnirs_driver_stats.spi_recovery_count,
        (unsigned long)fnirs_driver_stats.recovery_count,
        (int)fnirs_driver_stats.isr_core_id,
        (int)fnirs_driver_stats.consumer_core_id,
        (unsigned long)fnirs_driver_stats.max_irq_to_spi_start_us,
        (unsigned long)fnirs_driver_stats.max_spi_xfer_us,
        (unsigned long)fnirs_driver_stats.max_frame_service_us,
        (unsigned long)g_led_err_cnt,
        (unsigned long)g_led_on_max_us,
        (unsigned long)g_led_off_max_us,
        (unsigned long)led_stats.runtime_write_count,
        (unsigned long)led_stats.runtime_write_error_count,
        (unsigned long)led_stats.fast_path_write_count,
        (unsigned long)led_stats.fast_path_error_count,
        led_stats.fast_path_ready ? 1u : 0u,
        (int)led_stats.i2c_init_core_id,
        (int)led_stats.i2c_callback_core_id,
        (unsigned long)led_stats.hardware_reset_count,
        led_stats.ledout_state_known ? 1u : 0u,
        led_stats.device_ready ? 1u : 0u,
        g_led_fatal_fault ? 1u : 0u,
        (unsigned long long)timebase_status.alarm_irq_count,
        (unsigned long long)timebase_status.represented_tick_count,
        (unsigned long long)timebase_status.catchup_tick_count,
        (unsigned)timebase_status.max_isr_lateness_us,
        (unsigned)timebase_status.read_error_count,
        (unsigned)timebase_status.alarm_rearm_error_count,
        (int)timebase_status.init_core_id,
        (int)timebase_status.alarm_isr_core_id,
        core_plan_matches_runtime() ? 1u : 0u,
        (int)g_platform_init_core,
        (int)g_process_task_core,
        (int)scheduler_stats.task_core_id,
        (unsigned long)scheduler_stats.missed_tick_count,
        (unsigned long)scheduler_stats.late_tick_count,
        (unsigned long)scheduler_stats.max_tick_lateness_us,
        (unsigned long)scheduler_stats.aborted_slot_count,
        (unsigned long)scheduler_stats.abort_late_dispatch_count,
        (unsigned long)scheduler_stats.abort_begin_error_count,
        (unsigned long)scheduler_stats.abort_late_begin_completion_count,
        (unsigned long)scheduler_stats.slot_overrun_count,
        (unsigned long)scheduler_stats.max_callback_us,
        (unsigned long)g_stream_err_cnt,
        (unsigned long)nocti_stream_drop_fnirs(&g_stream),
        (unsigned)ADS131_SAMPLE_RATE_HZ,
        (unsigned long)g_adc_validated_sps,
        (unsigned long)g_adc_validation_min_period_us,
        (unsigned long)g_adc_validation_max_period_us,
        (unsigned)FNIRS_K_ON_TARGET,
        (unsigned)FNIRS_K_OFF_TARGET,
        (unsigned)FNIRS_K_ON_MIN_VALID,
        (unsigned)FNIRS_K_OFF_MIN_VALID,
        (unsigned)((g_on_min_n_used == UINT8_MAX) ? 0u : g_on_min_n_used),
        (unsigned)((g_off_min_n_used == UINT8_MAX) ? 0u : g_off_min_n_used),
        (unsigned)FNIRS_TARGET_WINDOW_US,
        (unsigned)FNIRS_MIN_VALID_WINDOW_US,
        (unsigned)FNIRS_ON_TIME_US,
        (unsigned)FNIRS_OFF_GAP_US,
        (unsigned)FNIRS_SETTLE_US,
        (unsigned long)((g_on_light_min_us == UINT32_MAX) ? 0u : g_on_light_min_us),
        (unsigned long)((g_on_stable_min_us == UINT32_MAX) ? 0u : g_on_stable_min_us),
        (unsigned long)((g_off_dark_min_us == UINT32_MAX) ? 0u : g_off_dark_min_us),
        (unsigned long)((g_off_stable_min_us == UINT32_MAX) ? 0u : g_off_stable_min_us),
        (unsigned long)g_process_job_count,
        (unsigned)process_queued,
        (unsigned long)g_process_queue_drop_count,
        (unsigned long)g_process_adc_wait_timeout_count,
        g_process_busy ? 1u : 0u,
        (unsigned)FNIRS_I2C_CLOCK_HZ,
        g_runtime_serial_quiet ? 1u : 0u,
        (unsigned long)g_runtime_health_serial_suppressed,
        (unsigned long)__atomic_load_n(&g_diag_trace_write_count, __ATOMIC_ACQUIRE),
        (unsigned)FNIRS_DIAG_TRACE_CAPACITY,
        (unsigned long)g_session_id);
}

static void send_trace(nocti_wifi_t *wifi, const nocti_wifi_peer_t *peer)
{
    if (!wifi || !peer || !g_ctrl_response_mutex) {
        return;
    }
    if (g_run || g_fnirs_adc_run || nocti_scheduler_is_running()) {
        send_ctrlf(wifi, peer, "FNIRS ERROR stop_before_TRACE\n");
        return;
    }
    if (xSemaphoreTake(g_ctrl_response_mutex, pdMS_TO_TICKS(100u)) != pdTRUE) {
        return;
    }

    const uint32_t total =
        __atomic_load_n(&g_diag_trace_write_count, __ATOMIC_ACQUIRE);
    const uint32_t available =
        (total > FNIRS_DIAG_TRACE_CAPACITY) ? FNIRS_DIAG_TRACE_CAPACITY : total;
    const uint32_t dump_count =
        (available > FNIRS_DIAG_TRACE_DUMP_MAX) ? FNIRS_DIAG_TRACE_DUMP_MAX : available;
    const uint32_t first_sequence =
        (dump_count == 0u) ? 0u : (total - dump_count + 1u);

    size_t used = 0u;
    int n = snprintf(g_ctrl_response,
                     sizeof(g_ctrl_response),
                     "FNIRS TRACE total=%lu available=%lu showing=%lu\n",
                     (unsigned long)total,
                     (unsigned long)available,
                     (unsigned long)dump_count);
    if (n > 0) used = (size_t)n;

    for (uint32_t sequence = first_sequence;
         sequence != 0u && sequence <= total && used < sizeof(g_ctrl_response) - 1u;
         sequence++) {
        const fnirs_diag_trace_event_t *event =
            &g_diag_trace[(sequence - 1u) % FNIRS_DIAG_TRACE_CAPACITY];
        const uint32_t committed =
            __atomic_load_n(&event->commit_sequence, __ATOMIC_ACQUIRE);
        if (committed != sequence) {
            continue;
        }
        n = snprintf(g_ctrl_response + used,
                     sizeof(g_ctrl_response) - used,
                     "%lu %llu %s v0=%lu v1=%lu\n",
                     (unsigned long)sequence,
                     (unsigned long long)event->timestamp_us,
                     diag_trace_type_name(event->type),
                     (unsigned long)event->value0,
                     (unsigned long)event->value1);
        if (n <= 0) break;
        if ((size_t)n >= sizeof(g_ctrl_response) - used) {
            used = sizeof(g_ctrl_response) - 1u;
            break;
        }
        used += (size_t)n;
    }

    if (used > 0u) {
        (void)nocti_wifi_send_ctrl_to(wifi, peer, g_ctrl_response, used);
    }
    xSemaphoreGive(g_ctrl_response_mutex);
}

static void log_stopped_summary(void)
{
    nocti_scheduler_stats_t scheduler_stats;
    nocti_fnirs_stats_t fnirs_stats;
    nocti_timebase_status_t timebase_status;
    memset(&scheduler_stats, 0, sizeof(scheduler_stats));
    memset(&fnirs_stats, 0, sizeof(fnirs_stats));
    memset(&timebase_status, 0, sizeof(timebase_status));
    nocti_scheduler_get_stats(&scheduler_stats);
    nocti_fnirs_get_stats(&fnirs_stats);
    nocti_timebase_get_status(&timebase_status);

    NOCTI_LOGI(TAG,
               "STOP summary session=%lu frames=%lu partial=%lu underk_on=%lu underk_off=%lu "
               "adc_ok=%lu adc_err=%lu missed=%lu crc_bad=%lu trace=%lu",
               (unsigned long)g_session_id,
               (unsigned long)g_frame_count,
               (unsigned long)g_partial_frame_count,
               (unsigned long)g_under_k_on_slot_count,
               (unsigned long)g_under_k_off_slot_count,
               (unsigned long)g_adc_ok_cnt,
               (unsigned long)g_adc_err_cnt,
               (unsigned long)g_adc_missed_drdy_count,
               (unsigned long)g_adc_crc_bad_cnt,
               (unsigned long)__atomic_load_n(&g_diag_trace_write_count, __ATOMIC_ACQUIRE));

    if (g_partial_frame_count != 0u ||
        g_adc_err_cnt != 0u ||
        g_adc_missed_drdy_count != 0u ||
        fnirs_stats.timing_fault_count != 0u ||
        fnirs_stats.recovery_count != 0u ||
        scheduler_stats.aborted_slot_count != 0u) {
        NOCTI_LOGW(TAG,
                   "STOP faults timing=%lu recovery=%lu irq2spi_max_us=%lu "
                   "spi_max_us=%lu sched_abort=%lu sched_cb_max_us=%lu",
                   (unsigned long)fnirs_stats.timing_fault_count,
                   (unsigned long)fnirs_stats.recovery_count,
                   (unsigned long)fnirs_stats.max_irq_to_spi_start_us,
                   (unsigned long)fnirs_stats.max_spi_xfer_us,
                   (unsigned long)scheduler_stats.aborted_slot_count,
                   (unsigned long)scheduler_stats.max_callback_us);
    } else {
        NOCTI_LOGI(TAG,
                   "STOP timing clean irq2spi_max_us=%lu spi_max_us=%lu tb_late_max_us=%u",
                   (unsigned long)fnirs_stats.max_irq_to_spi_start_us,
                   (unsigned long)fnirs_stats.max_spi_xfer_us,
                   (unsigned)timebase_status.max_isr_lateness_us);
    }
}

static void wifi_command_callback(nocti_wifi_t *self,
                                  const uint8_t *data,
                                  size_t len,
                                  const nocti_wifi_peer_t *from,
                                  void *user_ctx)
{
    (void)user_ctx;

    if (!self || !data || len == 0u || !from) {
        return;
    }

    char command[128];
    if (copy_normalized_command(command, sizeof(command), data, len) == 0u) {
        return;
    }

    if (strcmp(command, "START") == 0) {
        g_session_id = wifi_prepare_new_session(self, from);

        // Enter quiet mode before the ADS131 producer and optical scheduler are
        // enabled. The START reply remains on UDP and does not touch stdout.
        runtime_serial_set_quiet(true);
        const esp_err_t err = start_fnirs_acquisition();

        if (err == ESP_OK) {
            wifi_set_streaming_enabled(self, true);
            send_ctrlf(self,
                       from,
                       "FNIRS STARTED session=%lu mode=%s slot=%u current735=%u current850=%u serial_quiet=1\n",
                       (unsigned long)g_session_id,
                       test_mode_name(g_test_mode),
                       (unsigned)g_selected_slot,
                       (unsigned)g_current_735_ma,
                       (unsigned)g_current_850_ma);
        } else {
            wifi_set_streaming_enabled(self, false);
            stop_fnirs_acquisition();
            runtime_serial_set_quiet(false);
            send_ctrlf(self, from, "FNIRS ERROR start_failed=%s\n", esp_err_to_name(err));
            NOCTI_LOGE(TAG, "START failed: %s", esp_err_to_name(err));
        }
        return;
    }

    if (strcmp(command, "STOP") == 0) {
        wifi_set_streaming_enabled(self, false);
        stop_fnirs_acquisition();
        nocti_scheduler_stats_t scheduler_stats;
        memset(&scheduler_stats, 0, sizeof(scheduler_stats));
        nocti_scheduler_get_stats(&scheduler_stats);
        send_ctrlf(self,
                   from,
                   "FNIRS STOPPED frames=%lu partial=%lu underk_on=%lu underk_off=%lu "
                   "adc_ok=%lu adc_err=%lu adc_missed=%lu crc_bad=%lu "
                   "sched_missed=%lu sched_abort=%lu process_jobs=%lu process_drop=%lu "
                   "process_wait_timeout=%lu led_fatal=%u\n",
                   (unsigned long)g_frame_count,
                   (unsigned long)g_partial_frame_count,
                   (unsigned long)g_under_k_on_slot_count,
                   (unsigned long)g_under_k_off_slot_count,
                   (unsigned long)g_adc_ok_cnt,
                   (unsigned long)g_adc_err_cnt,
                   (unsigned long)g_adc_missed_drdy_count,
                   (unsigned long)g_adc_crc_bad_cnt,
                   (unsigned long)scheduler_stats.missed_tick_count,
                   (unsigned long)scheduler_stats.aborted_slot_count,
                   (unsigned long)g_process_job_count,
                   (unsigned long)g_process_queue_drop_count,
                   (unsigned long)g_process_adc_wait_timeout_count,
                   g_led_fatal_fault ? 1u : 0u);
        // Restore console levels only after all timing-critical work is stopped.
        runtime_serial_set_quiet(false);
        log_stopped_summary();
        return;
    }

    if (strcmp(command, "STATUS") == 0) {
        send_status(self, from);
        return;
    }

    if (strcmp(command, "TRACE") == 0) {
        send_trace(self, from);
        return;
    }

    if (strcmp(command, "HELP") == 0) {
        send_ctrlf(self,
                   from,
                   "FNIRS HELP: START | STOP | MODE DARK | MODE SINGLE | MODE SCAN | "
                   "SLOT 0..19 | CURRENT 735 0..%u | CURRENT 850 0..%u | STATUS | TRACE | REGS\n",
                   (unsigned)FNIRS_LED_CURRENT_LIMIT_MA,
                   (unsigned)FNIRS_LED_CURRENT_LIMIT_MA);
        return;
    }

    if (strcmp(command, "REGS") == 0) {
        if (g_run) {
            send_ctrlf(self, from, "FNIRS ERROR stop_before_REGS\n");
            return;
        }

        uint16_t mode = 0u;
        uint16_t clock = 0u;
        uint16_t gain = 0u;
        uint16_t cfg = 0u;
        const esp_err_t err = read_ads131_registers(&mode, &clock, &gain, &cfg);

        if (err == ESP_OK) {
            send_ctrlf(self,
                       from,
                       "FNIRS REGS MODE=0x%04X CLOCK=0x%04X GAIN=0x%04X CFG=0x%04X DRDY=%lu\n",
                       (unsigned)mode,
                       (unsigned)clock,
                       (unsigned)gain,
                       (unsigned)cfg,
                       (unsigned long)nocti_fnirs_dbg_drdy_count());
        } else {
            send_ctrlf(self, from, "FNIRS ERROR register_read=%s\n", esp_err_to_name(err));
        }
        return;
    }

    if (strncmp(command, "MODE ", 5u) == 0) {
        if (g_run) {
            send_ctrlf(self, from, "FNIRS ERROR stop_before_mode_change\n");
            return;
        }

        fnirs_test_mode_t new_mode;
        if (strcmp(command, "MODE DARK") == 0) {
            new_mode = FNIRS_TEST_MODE_DARK;
        } else if (strcmp(command, "MODE SINGLE") == 0) {
            new_mode = FNIRS_TEST_MODE_SINGLE;
        } else if (strcmp(command, "MODE SCAN") == 0) {
            new_mode = FNIRS_TEST_MODE_SCAN;
        } else {
            send_ctrlf(self, from, "FNIRS ERROR mode_must_be_DARK_SINGLE_or_SCAN\n");
            return;
        }

        g_test_mode = new_mode;
        (void)nocti_led_driver_all_off();
        send_ctrlf(self, from, "FNIRS MODE %s\n", test_mode_name(g_test_mode));
        return;
    }

    uint32_t value = 0u;
    if (parse_u32_argument(command, "SLOT ", &value)) {
        if (g_run) {
            send_ctrlf(self, from, "FNIRS ERROR stop_before_slot_change\n");
            return;
        }
        if (value >= FNIRS_SLOT_COUNT) {
            send_ctrlf(self, from, "FNIRS ERROR slot_range_0_to_19\n");
            return;
        }

        g_selected_slot = (uint8_t)value;
        send_ctrlf(self,
                   from,
                   "FNIRS SLOT %u led=%u wavelength=%u\n",
                   (unsigned)g_selected_slot,
                   (unsigned)(g_selected_slot / 2u),
                   ((g_selected_slot & 1u) == 0u) ? 735u : 850u);
        return;
    }

    if (parse_u32_argument(command, "CURRENT 735 ", &value)) {
        if (g_run) {
            send_ctrlf(self, from, "FNIRS ERROR stop_before_current_change\n");
            return;
        }
        if (value > FNIRS_LED_CURRENT_LIMIT_MA) {
            send_ctrlf(self,
                       from,
                       "FNIRS ERROR current_range_0_to_%u\n",
                       (unsigned)FNIRS_LED_CURRENT_LIMIT_MA);
            return;
        }

        const esp_err_t err = apply_led_currents((uint16_t)value, g_current_850_ma);
        if (err == ESP_OK) {
            send_ctrlf(self, from, "FNIRS CURRENT 735 %u mA\n", (unsigned)value);
        } else {
            send_ctrlf(self, from, "FNIRS ERROR current_apply=%s\n", esp_err_to_name(err));
        }
        return;
    }

    if (parse_u32_argument(command, "CURRENT 850 ", &value)) {
        if (g_run) {
            send_ctrlf(self, from, "FNIRS ERROR stop_before_current_change\n");
            return;
        }
        if (value > FNIRS_LED_CURRENT_LIMIT_MA) {
            send_ctrlf(self,
                       from,
                       "FNIRS ERROR current_range_0_to_%u\n",
                       (unsigned)FNIRS_LED_CURRENT_LIMIT_MA);
            return;
        }

        const esp_err_t err = apply_led_currents(g_current_735_ma, (uint16_t)value);
        if (err == ESP_OK) {
            send_ctrlf(self, from, "FNIRS CURRENT 850 %u mA\n", (unsigned)value);
        } else {
            send_ctrlf(self, from, "FNIRS ERROR current_apply=%s\n", esp_err_to_name(err));
        }
        return;
    }

    if (strcmp(command, "PING") == 0) {
        send_ctrlf(self, from, "PONG\n");
        return;
    }

    if (strcmp(command, "HELLO") == 0) {
        wifi_set_target(self, from);
        send_ctrlf(self, from, "NOCTI HELLO OK\n");
        return;
    }

    if (strcmp(command, "INFO") == 0) {
        send_ctrlf(self,
                   from,
                   "NOCTI FNIRS_TEST firmware=v15 ctrl=3333 stream=3334 modes=DARK,SINGLE,SCAN protocol=paired_v2_4_8k\n");
        return;
    }

    send_ctrlf(self, from, "FNIRS ERROR unknown_command; send HELP\n");
}

typedef struct {
    SemaphoreHandle_t done;
    bool timebase_ok;
    esp_err_t gpio_isr_service_err;
    int8_t core_id;
} fnirs_platform_init_ctx_t;

static void fnirs_platform_init_task_entry(void *arg)
{
    fnirs_platform_init_ctx_t *ctx = (fnirs_platform_init_ctx_t *)arg;
    if (!ctx) {
        vTaskDelete(NULL);
        return;
    }

    ctx->core_id = (int8_t)xPortGetCoreID();
    ctx->timebase_ok = nocti_timebase_init();

    // The core that installs the GPIO ISR service owns the external GPIO
    // interrupt. Installing it here keeps ADS131 DRDY ISR, GPTimer alarm ISR,
    // ADC task, and optical scheduler on acquisition core 1.
    // A pre-existing service is treated as an error here because its owning
    // core and IRAM flags cannot be proven. This fNIRS-only firmware installs
    // the service before board initialization, so success must be unambiguous.
    ctx->gpio_isr_service_err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);

    (void)xSemaphoreGive(ctx->done);
    vTaskDelete(NULL);
}

static esp_err_t init_acquisition_platform_on_core(void)
{
    fnirs_platform_init_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.core_id = -1;
    ctx.gpio_isr_service_err = ESP_FAIL;
    ctx.done = xSemaphoreCreateBinary();
    if (!ctx.done) return ESP_ERR_NO_MEM;

    const BaseType_t created = xTaskCreatePinnedToCore(
        fnirs_platform_init_task_entry,
        "fnirs_platform",
        FNIRS_PLATFORM_INIT_STACK_BYTES,
        &ctx,
        FNIRS_PLATFORM_INIT_PRIORITY,
        NULL,
        FNIRS_ACQ_CORE);
    if (created != pdPASS) {
        vSemaphoreDelete(ctx.done);
        return ESP_ERR_NO_MEM;
    }

    (void)xSemaphoreTake(ctx.done, portMAX_DELAY);
    vSemaphoreDelete(ctx.done);
    g_platform_init_core = ctx.core_id;

    if (ctx.core_id != FNIRS_ACQ_CORE) return ESP_ERR_INVALID_STATE;
    if (!ctx.timebase_ok) return ESP_FAIL;
    return ctx.gpio_isr_service_err;
}

// ============================================================
// Main
// ============================================================

void app_main(void)
{
    NOCTI_LOGI(TAG, "boot: fNIRS-only validation firmware");

    esp_err_t err = init_acquisition_platform_on_core();
    if (err != ESP_OK) {
        NOCTI_LOGE(TAG,
                   "acquisition platform init on core %d failed: %s",
                   FNIRS_ACQ_CORE,
                   esp_err_to_name(err));
        return;
    }

    // Initialize stream before the larger drivers to reduce heap fragmentation.
    nocti_stream_cfg_t stream_cfg;
    memset(&stream_cfg, 0, sizeof(stream_cfg));
    stream_cfg.rb_ctrl_bytes = (size_t)(8u * 1024u);
    stream_cfg.rb_fnirs_bytes = (size_t)(48u * 1024u);
    stream_cfg.rb_biopot_bytes = (size_t)(4u * 1024u);

    err = nocti_stream_init_now(&g_stream, &stream_cfg);
    if (err != ESP_OK) {
        NOCTI_LOGE(TAG, "stream init failed: %s", esp_err_to_name(err));
        return;
    }

    nocti_board_cfg_t board_cfg;
    nocti_board_cfg_set_defaults(&board_cfg);

    // NoctiNIRS v1 has external 4.7 kOhm pull-ups on SDA/SCL. Keep the
    // ESP32-S3 bus at the documented 400 kHz operating point and disable the
    // weaker internal pull-ups so the PCB resistors define the bus edges.
    board_cfg.i2c_clk_hz = FNIRS_I2C_CLOCK_HZ;
    // Boot/configuration must be truly synchronous. The LED driver recreates
    // this bus with FNIRS_I2C_ASYNC_QUEUE_DEPTH only after PCA readback passes.
    board_cfg.i2c_trans_queue_depth = 0u;
    board_cfg.i2c_enable_internal_pullups = false;

    board_cfg.spi_host_ads1299 = SPI2_HOST;
    board_cfg.spi_host_ads131 = SPI3_HOST;

    // nocti_board_init_all() initializes both SPI buses. ADS1299 remains unused
    // in this test firmware, but its bus still needs a valid host selection.
    board_cfg.spi_ads1299.clock_hz = 4u * 1000u * 1000u;
    board_cfg.spi_ads1299.spi_mode = 1u;
    board_cfg.spi_ads1299.use_dma = false;

    board_cfg.spi_ads131.clock_hz = FNIRS_ADS131_SPI_CLOCK_HZ;
    board_cfg.spi_ads131.spi_mode = 1u;
    board_cfg.spi_ads131.use_dma = false;
    board_cfg.spi_ads131.queue_size = 1u;

    err = nocti_board_init_all(&g_board, &board_cfg);
    if (err != ESP_OK) {
        (void)nocti_stream_deinit(&g_stream);
        NOCTI_LOGE(TAG, "board init failed: %s", esp_err_to_name(err));
        return;
    }

    const nocti_board_pins_t *pins = nocti_board_pins(&g_board);
    if (!pins) {
        NOCTI_LOGE(TAG, "board pins unavailable");
        return;
    }

    build_slot_table();

    nocti_led_driver_cfg_t led_cfg;
    nocti_led_driver_cfg_set_defaults(&led_cfg);
    led_cfg.i2c = nocti_board_i2c(&g_board);
    led_cfg.i2c_addr_7bit = nocti_board_i2c_addrs(&g_board)->pca9956b_7bit;
    led_cfg.rext_ohm = 1000u;
    led_cfg.use_nocti_default_map = true;
    led_cfg.default_current_ma_735 = FNIRS_DEFAULT_CURRENT_735_MA;
    led_cfg.default_current_ma_850 = FNIRS_DEFAULT_CURRENT_850_MA;
    led_cfg.max_current_ma = FNIRS_LED_CURRENT_LIMIT_MA;
    led_cfg.apply_current_in_slot = false;
    led_cfg.force_off_before_on = false;
    led_cfg.cleanup_missed_slot_end = true;
    led_cfg.force_disable_pwm_regs = true;
    // Every normal slot has an explicit OFF edge before the next ON edge.
    // Single-register LEDOUT writes reduce a 7-byte burst to one register write
    // at 400 kHz, preserving the K_min=8 timing margin. cleanup_missed_slot_end keeps
    // the safety invariant: an old channel is switched OFF before a new one.
    led_cfg.atomic_one_hot_slots = false;
    led_cfg.enable_async_fast_path = true;
    led_cfg.async_queue_depth = FNIRS_I2C_ASYNC_QUEUE_DEPTH;
    led_cfg.init_i2c_timeout_ms = FNIRS_LED_INIT_I2C_TIMEOUT_MS;
    led_cfg.runtime_i2c_timeout_ms = FNIRS_LED_RUN_I2C_TIMEOUT_MS;
    led_cfg.verify_init_registers = true;
    led_cfg.gpio_reset = pins->pca_reset;
    led_cfg.hardware_reset_fail_safe = true;
    led_cfg.hardware_reset_low_us = FNIRS_LED_RESET_LOW_US;
    led_cfg.hardware_reset_recovery_us = FNIRS_LED_RESET_RECOVERY_US;

    err = run_led_control_on_acq_core(FNIRS_LED_CONTROL_INIT, &led_cfg);
    if (err != ESP_OK) {
        NOCTI_LOGE(TAG, "LED driver init failed: %s", esp_err_to_name(err));
        return;
    }

    err = nocti_led_driver_all_off();
    if (err != ESP_OK) {
        NOCTI_LOGE(TAG, "unable to force LEDs off: %s", esp_err_to_name(err));
        return;
    }

    err = apply_led_currents(FNIRS_DEFAULT_CURRENT_735_MA,
                             FNIRS_DEFAULT_CURRENT_850_MA);
    if (err != ESP_OK) {
        NOCTI_LOGE(TAG, "unable to apply default LED currents: %s", esp_err_to_name(err));
        return;
    }

    log_led_mapping();

    nocti_fnirs_cfg_t fnirs_cfg;
    nocti_fnirs_cfg_set_defaults(&fnirs_cfg);
    fnirs_cfg.spi = nocti_board_spi_ads131(&g_board);
    fnirs_cfg.gpio_drdy = pins->ads131_drdy;
    fnirs_cfg.gpio_reset = pins->ads131_sync_reset;
    fnirs_cfg.gpio_sync = pins->ads131_sync_reset;
    fnirs_cfg.drdy_timeout_us = 5000u;
    fnirs_cfg.expected_drdy_period_us = ADS131_EXPECTED_PERIOD_US;
    fnirs_cfg.drdy_period_tolerance_us = 40u;
    fnirs_cfg.spi_timeout_ms = 20u;
    fnirs_cfg.crc_check_enable = true;
    // Do not perform an immediate second SPI frame read after CRC failure:
    // ADS131 is a continuous converter, so a retry can consume the next DRDY
    // frame and hide the actual sample gap. Record the bad frame instead.
    fnirs_cfg.crc_retry = 0u;
    fnirs_cfg.do_adc_register_init = true;
    fnirs_cfg.reg_mode = 0u;
    fnirs_cfg.reg_clock = ADS131_CLOCK_8000_SPS;
    fnirs_cfg.reg_gain = 0u;
    fnirs_cfg.queue_len = 0u;

    err = nocti_fnirs_init(&fnirs_cfg);
    if (err != ESP_OK) {
        (void)nocti_led_driver_all_off();
        NOCTI_LOGE(TAG, "fNIRS ADC init failed: %s", esp_err_to_name(err));
        return;
    }

    // Explicitly restore the ADS131M04 CFG reset value used for continuous
    // conversion. This guarantees GC_EN=0 even when the MCU is reflashed or
    // restarted without a full board power cycle after the former WREG-address
    // bug had written a CLOCK value into CFG.
    err = nocti_fnirs_dbg_write_reg_u16(ADS131_REG_CFG_ADDR,
                                        ADS131_CFG_CONTINUOUS_DEFAULT);
    if (err != ESP_OK) {
        (void)nocti_led_driver_all_off();
        NOCTI_LOGE(TAG, "ADS131 CFG continuous-mode restore failed: %s",
                   esp_err_to_name(err));
        return;
    }

    err = validate_ads131_registers();
    if (err != ESP_OK) {
        (void)nocti_led_driver_all_off();
        NOCTI_LOGE(TAG, "ADS131 validation failed: %s", esp_err_to_name(err));
        return;
    }

    err = validate_drdy_rate_on_acq_core();
    if (err != ESP_OK) {
        (void)nocti_led_driver_all_off();
        return;
    }
    nocti_fnirs_reset_stats();

    g_fnirs_process_queue = xQueueCreate(FNIRS_PROCESS_QUEUE_LEN,
                                         sizeof(fnirs_slot_job_t));
    g_fnirs_process_mutex = xSemaphoreCreateMutex();
    if (g_fnirs_process_queue == NULL || g_fnirs_process_mutex == NULL) {
        (void)nocti_led_driver_all_off();
        NOCTI_LOGE(TAG, "fNIRS processing queue/mutex creation failed");
        return;
    }

    const BaseType_t process_task_ok = xTaskCreatePinnedToCore(
        fnirs_process_task_entry,
        "fnirs_process",
        FNIRS_PROCESS_TASK_STACK_BYTES,
        NULL,
        FNIRS_PROCESS_TASK_PRIORITY,
        &g_fnirs_process_task,
        FNIRS_PROCESS_CORE);

    if (process_task_ok != pdPASS) {
        (void)nocti_led_driver_all_off();
        NOCTI_LOGE(TAG, "fNIRS processing task creation failed");
        return;
    }

    nocti_scheduler_cfg_t scheduler_cfg;
    memset(&scheduler_cfg, 0, sizeof(scheduler_cfg));
    scheduler_cfg.slots = g_slots;
    scheduler_cfg.slot_count = FNIRS_SLOT_COUNT;
    scheduler_cfg.tick_period_us = FNIRS_TICK_PERIOD_US;
    scheduler_cfg.frame_period_us = FNIRS_FRAME_PERIOD_US;
    scheduler_cfg.dark_us_total = 0u;
    scheduler_cfg.dark_mode = NOCTI_DARK_INTERLEAVED;
    scheduler_cfg.slot_end_guard_us = FNIRS_SCHED_END_GUARD_US;
    scheduler_cfg.late_warn_us = FNIRS_SCHED_LATE_WARN_US;
    scheduler_cfg.priority = FNIRS_SCHED_TASK_PRIORITY;
    scheduler_cfg.stack_bytes = 4096u;
    scheduler_cfg.pin_to_core = true;
    scheduler_cfg.core_id = FNIRS_ACQ_CORE;

    if (!nocti_scheduler_init(&scheduler_cfg, scheduler_callback, NULL)) {
        (void)nocti_led_driver_all_off();
        NOCTI_LOGE(TAG, "scheduler init failed");
        return;
    }

    const BaseType_t task_ok = xTaskCreatePinnedToCore(
        fnirs_adc_task_entry,
        "fnirs_adc",
        FNIRS_ADC_TASK_STACK_BYTES,
        NULL,
        FNIRS_ADC_TASK_PRIORITY,
        &g_fnirs_adc_task,
        FNIRS_ACQ_CORE);

    if (task_ok != pdPASS) {
        (void)nocti_led_driver_all_off();
        NOCTI_LOGE(TAG, "fNIRS ADC task creation failed");
        return;
    }

    g_ctrl_response_mutex = xSemaphoreCreateMutex();
    if (g_ctrl_response_mutex == NULL) {
        (void)nocti_led_driver_all_off();
        NOCTI_LOGE(TAG, "control response mutex creation failed");
        return;
    }

    nocti_wifi_cfg_t wifi_cfg;
    nocti_wifi_cfg_set_defaults(&wifi_cfg);
    wifi_cfg.ap_ssid = "REFERENCE_WIFI_SSID";
    wifi_cfg.ap_pass = "REFERENCE_WIFI_PASSWORD";
    wifi_cfg.ap_channel = 6u;
    wifi_cfg.ap_max_conn = 1u;
    wifi_cfg.udp_ctrl_port = 3333u;
    wifi_cfg.udp_stream_port = 3334u;
    wifi_cfg.enable_builtin_cmd = false;
    wifi_cfg.enable_advert = true;
    wifi_cfg.advert_period_ms = 1000u;
    wifi_cfg.stream = &g_stream;
    wifi_cfg.enable_stream_tx = true;
    wifi_cfg.stream_read_timeout_ms = 50u;
    wifi_cfg.use_udp_header = true;
    wifi_cfg.reset_session_on_start = false;
    wifi_cfg.cmd_cb = wifi_command_callback;
    wifi_cfg.cmd_cb_ctx = NULL;
    // ESP-IDF task stack arguments are bytes, not words.  The control task
    // calls the command parser, printf formatter, and lwIP send path, so keep
    // a conservative margin even though the large response buffer is in BSS.
    wifi_cfg.ctrl_task_stack_words = 8192u;
    wifi_cfg.adv_task_stack_words = 3072u;
    wifi_cfg.tx_task_stack_words = 4096u;
    wifi_cfg.ctrl_task_prio = 9u;
    wifi_cfg.tx_task_prio = 8u;
    wifi_cfg.adv_task_prio = 7u;

    err = nocti_wifi_init(&g_wifi, &wifi_cfg);
    if (err != ESP_OK) {
        (void)nocti_led_driver_all_off();
        NOCTI_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(err));
        return;
    }

    fnirs_frame_reset();

    nocti_timebase_status_t boot_timebase_status;
    nocti_fnirs_stats_t boot_fnirs_stats;
    nocti_led_driver_stats_t boot_led_stats;
    memset(&boot_timebase_status, 0, sizeof(boot_timebase_status));
    memset(&boot_fnirs_stats, 0, sizeof(boot_fnirs_stats));
    memset(&boot_led_stats, 0, sizeof(boot_led_stats));
    nocti_timebase_get_status(&boot_timebase_status);
    nocti_fnirs_get_stats(&boot_fnirs_stats);
    nocti_led_driver_get_stats(&boot_led_stats);

    NOCTI_LOGI(TAG, "ready");
    NOCTI_LOGI(TAG, "SSID=NOCTINIRS password=12345678");
    NOCTI_LOGI(TAG, "UDP control=3333 stream=3334");
    NOCTI_LOGI(TAG,
               "core plan: core1=GPTimer+DRDY+ADC+scheduler+LED-I2C, "
               "core0=processing+WiFi; observed platform=%d tb_init=%d "
               "drdy_isr=%d adc_consumer=%d led_i2c_init=%d led_i2c_cb=%d",
               (int)g_platform_init_core,
               (int)boot_timebase_status.init_core_id,
               (int)boot_fnirs_stats.isr_core_id,
               (int)boot_fnirs_stats.consumer_core_id,
               (int)boot_led_stats.i2c_init_core_id,
               (int)boot_led_stats.i2c_callback_core_id);
    NOCTI_LOGI(TAG,
               "optical timing: 20 slots x 5 ms, LED-OFF command deadline=%u us, "
               "nominal OFF command budget=%u us, settle=%u us, target_window=%u us, "
               "min_valid_window=%u us, end_guard=%u us; edges use post-STOP ISR timestamps",
               (unsigned)FNIRS_ON_TIME_US,
               (unsigned)FNIRS_OFF_GAP_US,
               (unsigned)FNIRS_SETTLE_US,
               (unsigned)FNIRS_TARGET_WINDOW_US,
               (unsigned)FNIRS_MIN_VALID_WINDOW_US,
               (unsigned)FNIRS_SCHED_END_GUARD_US);
    NOCTI_LOGI(TAG,
               "LED bus: %u Hz, external pull-ups, phased sync-init -> async-runtime queue=%u, "
               "persistent PCA fast-path; edge timestamp captured in I2C ISR",
               (unsigned)FNIRS_I2C_CLOCK_HZ,
               (unsigned)FNIRS_I2C_ASYNC_QUEUE_DEPTH);
    NOCTI_LOGI(TAG,
               "LED fail-safe: RESET# GPIO%d, low=%u us, recovery=%u us",
               pins->pca_reset,
               (unsigned)FNIRS_LED_RESET_LOW_US,
               (unsigned)FNIRS_LED_RESET_RECOVERY_US);
    NOCTI_LOGI(TAG,
               "ADS131=%u SPS, SPI=%u Hz, expected period=%u us, "
               "K target(on/off)=%u/%u, K minimum-valid=%u/%u; "
               "DRDY=pulse, one ISR event timestamp per frame, FIFO-risk SYNC recovery",
               (unsigned)ADS131_SAMPLE_RATE_HZ,
               (unsigned)FNIRS_ADS131_SPI_CLOCK_HZ,
               (unsigned)ADS131_EXPECTED_PERIOD_US,
               (unsigned)FNIRS_K_ON_TARGET,
               (unsigned)FNIRS_K_OFF_TARGET,
               (unsigned)FNIRS_K_ON_MIN_VALID,
               (unsigned)FNIRS_K_OFF_MIN_VALID);
    NOCTI_LOGI(TAG,
               "default mode=DARK; runtime serial quiet=%u, health via UDP STATUS, TRACE after STOP",
               (unsigned)FNIRS_RUNTIME_SERIAL_QUIET);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(FNIRS_RUNTIME_HEALTH_PERIOD_MS));

        // All counters continue to be updated by their owning tasks. Runtime
        // health is intentionally pull-based through UDP STATUS; no periodic
        // serial formatting or console write is performed while g_run is true.
        if (g_run) {
            g_runtime_health_serial_suppressed++;
        }
    }
}
