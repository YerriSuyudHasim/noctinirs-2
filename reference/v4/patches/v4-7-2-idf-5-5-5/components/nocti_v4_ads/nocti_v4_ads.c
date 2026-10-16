#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "nocti_v4_ads.h"

#include <limits.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "nocti_v4_ads";

#define EVENT_RING_MASK (NOCTI_V4_DRDY_EVENT_RING_CAPACITY - 1u)
#define RAW_RING_MASK   (NOCTI_V4_RAW_SAMPLE_RING_CAPACITY - 1u)

#define ADS_REG_ID       0x00u
#define ADS_REG_STATUS   0x01u
#define ADS_REG_MODE     0x02u
#define ADS_REG_CLOCK    0x03u
#define ADS_REG_GAIN     0x04u
#define ADS_REG_CFG      0x06u
#define ADS_REG_CH0_CFG  0x09u
#define ADS_CHANNEL_REGISTER_STRIDE 0x05u
#define ADS_CHANNEL_OCAL_MSB_OFFSET 0x01u
#define ADS_CHANNEL_OCAL_LSB_OFFSET 0x02u
#define ADS_CHANNEL_GCAL_MSB_OFFSET 0x03u
#define ADS_CHANNEL_GCAL_LSB_OFFSET 0x04u

#define ADS_CH_CFG_PHASE_MASK       0xFFC0u
#define ADS_CH_CFG_DCBLK_DISABLE    0x0004u
#define ADS_CH_CFG_MUX_MASK         0x0003u
#define ADS_CH_CFG_MUX_EXTERNAL     0x0000u
#define ADS_CH_CFG_MUX_POSITIVE_DC  0x0002u
#define ADS_GCAL_UNITY_RAW          0x800000u
#define ADS_STATUS_DRDY_MASK        0x000Fu
#define ADS_STATUS_DRDY0_MASK       0x0001u

#define ADS_CHANNEL_REG(channel, offset) \
    ((uint8_t)(ADS_REG_CH0_CFG + \
               (uint8_t)(channel) * ADS_CHANNEL_REGISTER_STRIDE + \
               (uint8_t)(offset)))

_Static_assert(ADS_CHANNEL_REGISTER_STRIDE == 5u &&
               ADS_CHANNEL_REG(0u, 0u) == 0x09u &&
               ADS_CHANNEL_REG(1u, 0u) == 0x0Eu &&
               ADS_CHANNEL_REG(2u, 0u) == 0x13u &&
               ADS_CHANNEL_REG(3u, 0u) == 0x18u &&
               ADS_CHANNEL_REG(0u, ADS_CHANNEL_OCAL_MSB_OFFSET) == 0x0Au &&
               ADS_CHANNEL_REG(0u, ADS_CHANNEL_OCAL_LSB_OFFSET) == 0x0Bu &&
               ADS_CHANNEL_REG(0u, ADS_CHANNEL_GCAL_MSB_OFFSET) == 0x0Cu &&
               ADS_CHANNEL_REG(0u, ADS_CHANNEL_GCAL_LSB_OFFSET) == 0x0Du &&
               ADS_CHANNEL_REG(1u, ADS_CHANNEL_OCAL_MSB_OFFSET) == 0x0Fu &&
               ADS_CHANNEL_REG(1u, ADS_CHANNEL_OCAL_LSB_OFFSET) == 0x10u &&
               ADS_CHANNEL_REG(1u, ADS_CHANNEL_GCAL_MSB_OFFSET) == 0x11u &&
               ADS_CHANNEL_REG(1u, ADS_CHANNEL_GCAL_LSB_OFFSET) == 0x12u &&
               ADS_CHANNEL_REG(2u, ADS_CHANNEL_OCAL_MSB_OFFSET) == 0x14u &&
               ADS_CHANNEL_REG(2u, ADS_CHANNEL_OCAL_LSB_OFFSET) == 0x15u &&
               ADS_CHANNEL_REG(2u, ADS_CHANNEL_GCAL_MSB_OFFSET) == 0x16u &&
               ADS_CHANNEL_REG(2u, ADS_CHANNEL_GCAL_LSB_OFFSET) == 0x17u &&
               ADS_CHANNEL_REG(3u, ADS_CHANNEL_OCAL_MSB_OFFSET) == 0x19u &&
               ADS_CHANNEL_REG(3u, ADS_CHANNEL_OCAL_LSB_OFFSET) == 0x1Au &&
               ADS_CHANNEL_REG(3u, ADS_CHANNEL_GCAL_MSB_OFFSET) == 0x1Bu &&
               ADS_CHANNEL_REG(3u, ADS_CHANNEL_GCAL_LSB_OFFSET) == 0x1Cu,
               "ADS channel register blocks have a five-register stride");

#define ADS_FRAME_STATUS_OFFSET 0u
#define ADS_FRAME_CH0_OFFSET    3u
#define ADS_FRAME_CH1_OFFSET    6u
#define ADS_FRAME_CH2_OFFSET    9u
#define ADS_FRAME_CH3_OFFSET    12u
#define ADS_FRAME_CRC_OFFSET    15u

_Static_assert(ADS_FRAME_CH0_OFFSET == 1u * NOCTI_V4_ADS_WORD_BYTES &&
               ADS_FRAME_CH1_OFFSET == 2u * NOCTI_V4_ADS_WORD_BYTES &&
               ADS_FRAME_CH2_OFFSET == 3u * NOCTI_V4_ADS_WORD_BYTES &&
               ADS_FRAME_CH3_OFFSET == 4u * NOCTI_V4_ADS_WORD_BYTES &&
               ADS_FRAME_CRC_OFFSET == 5u * NOCTI_V4_ADS_WORD_BYTES,
               "ADS frame is STATUS, CH0, CH1, CH2, CH3, CRC");

#define ADS_CMD_NULL     0x0000u
#define ADS_CMD_UNLOCK   0x0655u

#define ADS_MODE_WLENGTH_MASK  0x0300u
#define ADS_MODE_WLENGTH_24    0x0100u
#define ADS_MODE_RX_CRC_MASK   0x1000u
#define ADS_MODE_RESET_MASK    0x0400u
#define ADS_MODE_CRC_TYPE_MASK 0x0800u
#define ADS_MODE_DRDY_HIZ_MASK 0x0002u
#define ADS_MODE_DRDY_FMT_MASK 0x0001u
#define ADS_MODE_DRDY_PULSE    0x0001u

#define ADS_ID_CHANCNT_MASK    0x0F00u
#define ADS_ID_CHANCNT_4       0x0400u
#define ADS_RESET_RESPONSE     0xFF24u
#define ADS_STATUS_LOCK_MASK      0x8000u
#define ADS_STATUS_F_RESYNC_MASK  0x4000u
#define ADS_STATUS_REG_MAP_MASK   0x2000u
#define ADS_STATUS_CRC_ERR_MASK   0x1000u
#define ADS_STATUS_CRC_TYPE_MASK  0x0800u
#define ADS_STATUS_RESET_MASK     0x0400u
#define ADS_STATUS_HARD_FAULT_MASK \
    (ADS_STATUS_LOCK_MASK | ADS_STATUS_REG_MAP_MASK | \
     ADS_STATUS_CRC_ERR_MASK | ADS_STATUS_RESET_MASK)

#define ADS_CMD_RREG(addr, n_minus1) \
    ((uint16_t)((0x5u << 13) | (((uint16_t)(addr) & 0x3Fu) << 7) | \
                ((uint16_t)(n_minus1) & 0x7Fu)))
#define ADS_CMD_WREG(addr, n_minus1) \
    ((uint16_t)((0x3u << 13) | (((uint16_t)(addr) & 0x3Fu) << 7) | \
                ((uint16_t)(n_minus1) & 0x7Fu)))

_Static_assert(ADS_CMD_RREG(ADS_REG_MODE, 0u) == 0xA100u,
               "ADS RREG address field is bits [12:7]");
_Static_assert(ADS_CMD_RREG(ADS_REG_CLOCK, 0u) == 0xA180u,
               "ADS RREG CLOCK must be 0xA180");
_Static_assert(ADS_CMD_WREG(ADS_REG_MODE, 0u) == 0x6100u,
               "ADS WREG MODE must be 0x6100");
_Static_assert(ADS_CMD_WREG(ADS_REG_CLOCK, 0u) == 0x6180u,
               "ADS WREG CLOCK must be 0x6180");

typedef struct {
    uint64_t timestamp_us;
    uint32_t sequence;
    uint32_t session_epoch;
} drdy_event_t;

typedef struct {
    uint16_t status;
    int32_t channel[NOCTI_V4_ADS_CHANNEL_COUNT];
    uint16_t crc_received;
    bool crc_valid;
    bool status_valid;
    bool resync_occurred;
} ads_frame_t;

typedef struct {
    bool bus_initialized;
    bool device_added;
    bool bus_acquired;
    spi_device_handle_t device;
    spi_transaction_t transaction;
    uint8_t tx[NOCTI_V4_ADS_FRAME_BYTES];
    uint8_t rx[NOCTI_V4_ADS_FRAME_BYTES];
} ads_transport_t;

typedef enum {
    ADS_COMMAND_START_DARK = 1,
    ADS_COMMAND_STOP_DARK,
    ADS_COMMAND_FRAME_DUMP,
    ADS_COMMAND_CHANNEL_REGS,
    ADS_COMMAND_CH0_SELFTEST,
} ads_command_type_t;

typedef struct {
    ads_command_type_t type;
    uint8_t frame_count;
} ads_command_t;

typedef struct {
    nocti_v4_ads_t *public_ads;
    nocti_v4_board_t *board;
    nocti_v4_time_t *timebase;
    nocti_v4_health_t *health;
    ads_transport_t transport;

    TaskHandle_t ads_task;
    TaskHandle_t dark_task;
    SemaphoreHandle_t init_done;
    SemaphoreHandle_t command_done;
    SemaphoreHandle_t command_lock;
    SemaphoreHandle_t drain_done;
    StaticSemaphore_t init_done_storage;
    StaticSemaphore_t command_done_storage;
    StaticSemaphore_t command_lock_storage;
    StaticSemaphore_t drain_done_storage;
    QueueHandle_t command_queue;
    StaticQueue_t command_queue_storage;
    uint8_t command_queue_buffer[4u * sizeof(ads_command_t)];

    volatile bool initialized;
    volatile bool ready;
    volatile bool rate_valid;
    volatile bool capture_enabled;
    volatile bool session_integrity_fault;
    volatile bool event_overflow_pending;
    volatile bool drain_requested;
    volatile nocti_v4_ads_lifecycle_t lifecycle;
    volatile esp_err_t init_result;
    volatile esp_err_t command_result;
    volatile esp_err_t last_error;
    volatile uint32_t session_epoch;
    volatile uint32_t drdy_sequence;
    volatile uint32_t event_head;
    volatile uint32_t event_tail;
    volatile uint32_t raw_head;
    volatile uint32_t raw_tail;
    volatile uint32_t event_depth_max;
    volatile uint32_t raw_depth_max;
    volatile int32_t gpio_isr_callback_core;
    volatile uint32_t last_drdy_timestamp_version;
    volatile uint32_t last_drdy_timestamp_low;
    volatile uint32_t last_drdy_timestamp_high;
    volatile uint32_t last_drdy_sequence;
    volatile nocti_v4_ads_fault_code_t isr_capture_fault_code;
    volatile nocti_v4_ads_fault_code_t last_fault_code;
    volatile nocti_v4_ads_fault_origin_t last_fault_origin;
    volatile esp_err_t last_fault_error;
    /* Exactly one task owns raw-ring pops. The ADS task only produces. */
    TaskHandle_t volatile raw_consumer_task;
    volatile nocti_v4_ads_raw_consumer_mode_t raw_consumer_mode;
    portMUX_TYPE raw_stats_lock;

    drdy_event_t event_ring[NOCTI_V4_DRDY_EVENT_RING_CAPACITY];
    nocti_v4_raw_sample_t raw_ring[NOCTI_V4_RAW_SAMPLE_RING_CAPACITY];
    nocti_v4_ads_register_snapshot_t registers;

    uint32_t measured_sps;
    uint32_t validation_period_avg_us;
    uint32_t validation_period_min_us;
    uint32_t validation_period_max_us;
    uint32_t validation_samples;
    uint32_t gpio_isr_install_core;
    int32_t ads_task_core;
    int32_t dark_task_core;
    uint32_t sync_discard_remaining;
    uint32_t expected_resync_remaining;
    uint32_t last_sequence;
    uint64_t last_timestamp_us;
    uint64_t run_start_timestamp_us;
    uint64_t period_sum_us;
    uint32_t period_count;
    uint64_t spi_duration_sum_us;
    uint32_t spi_duration_count;
    uint32_t runtime_period_count;
    uint32_t runtime_period_us[NOCTI_V4_ADS_RUNTIME_PERIOD_SNAPSHOT_COUNT];
    uint32_t raw_stats_work_count;
    uint32_t raw_stats_window_sequence;
    int32_t raw_stats_minimum[NOCTI_V4_ADS_CHANNEL_COUNT];
    int32_t raw_stats_maximum[NOCTI_V4_ADS_CHANNEL_COUNT];
    int64_t raw_stats_sum[NOCTI_V4_ADS_CHANNEL_COUNT];
    nocti_v4_ads_raw_stats_snapshot_t raw_stats_published;
    nocti_v4_ads_frame_dump_snapshot_t frame_dump;
    nocti_v4_ads_channel_register_snapshot_t channel_registers;
    nocti_v4_ads_ch0_selftest_snapshot_t ch0_selftest;
} ads_runtime_t;

static ads_runtime_t s_runtime;
static bool s_runtime_claimed;

static inline void counter_add(uint32_t *counter, uint32_t amount)
{
    (void)__atomic_fetch_add(counter, amount, __ATOMIC_RELAXED);
}

static inline void counter_store(uint32_t *counter, uint32_t value)
{
    __atomic_store_n(counter, value, __ATOMIC_RELEASE);
}

static void update_max(uint32_t *counter, uint32_t candidate)
{
    uint32_t current = __atomic_load_n(counter, __ATOMIC_RELAXED);
    while (candidate > current &&
           !__atomic_compare_exchange_n(counter, &current, candidate, false,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

static void update_min(uint32_t *counter, uint32_t candidate)
{
    uint32_t current = __atomic_load_n(counter, __ATOMIC_RELAXED);
    while ((current == 0u || candidate < current) &&
           !__atomic_compare_exchange_n(counter, &current, candidate, false,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

static void reset_raw_stats(ads_runtime_t *runtime)
{
    runtime->raw_stats_work_count = 0u;
    runtime->raw_stats_window_sequence = 0u;
    memset(runtime->raw_stats_minimum, 0, sizeof(runtime->raw_stats_minimum));
    memset(runtime->raw_stats_maximum, 0, sizeof(runtime->raw_stats_maximum));
    memset(runtime->raw_stats_sum, 0, sizeof(runtime->raw_stats_sum));
    portENTER_CRITICAL(&runtime->raw_stats_lock);
    memset(&runtime->raw_stats_published, 0,
           sizeof(runtime->raw_stats_published));
    portEXIT_CRITICAL(&runtime->raw_stats_lock);
}

static void record_raw_stats(ads_runtime_t *runtime,
                             const nocti_v4_raw_sample_t *sample)
{
    if (runtime->raw_stats_work_count == 0u) {
        for (uint8_t channel = 0u; channel < NOCTI_V4_ADS_CHANNEL_COUNT; ++channel) {
            runtime->raw_stats_minimum[channel] = sample->channel[channel];
            runtime->raw_stats_maximum[channel] = sample->channel[channel];
            runtime->raw_stats_sum[channel] = 0;
        }
    }
    for (uint8_t channel = 0u; channel < NOCTI_V4_ADS_CHANNEL_COUNT; ++channel) {
        const int32_t value = sample->channel[channel];
        if (value < runtime->raw_stats_minimum[channel]) {
            runtime->raw_stats_minimum[channel] = value;
        }
        if (value > runtime->raw_stats_maximum[channel]) {
            runtime->raw_stats_maximum[channel] = value;
        }
        runtime->raw_stats_sum[channel] += value;
    }
    ++runtime->raw_stats_work_count;
    if (runtime->raw_stats_work_count != NOCTI_V4_ADS_RAW_STATS_WINDOW_SAMPLES) {
        return;
    }

    nocti_v4_ads_raw_stats_snapshot_t completed = {
        .valid = true,
        .session_epoch = sample->session_epoch,
        .window_sequence = ++runtime->raw_stats_window_sequence,
        .count = runtime->raw_stats_work_count,
    };
    for (uint8_t channel = 0u; channel < NOCTI_V4_ADS_CHANNEL_COUNT; ++channel) {
        completed.minimum[channel] = runtime->raw_stats_minimum[channel];
        completed.maximum[channel] = runtime->raw_stats_maximum[channel];
        completed.mean[channel] = runtime->raw_stats_sum[channel] /
                                  (int64_t)runtime->raw_stats_work_count;
    }
    portENTER_CRITICAL(&runtime->raw_stats_lock);
    runtime->raw_stats_published = completed;
    portEXIT_CRITICAL(&runtime->raw_stats_lock);
    runtime->raw_stats_work_count = 0u;
}

static uint32_t event_depth(const ads_runtime_t *runtime)
{
    const uint32_t head = __atomic_load_n(&runtime->event_head, __ATOMIC_ACQUIRE);
    const uint32_t tail = __atomic_load_n(&runtime->event_tail, __ATOMIC_ACQUIRE);
    return head - tail;
}

static uint32_t raw_depth(const ads_runtime_t *runtime)
{
    const uint32_t head = __atomic_load_n(&runtime->raw_head, __ATOMIC_ACQUIRE);
    const uint32_t tail = __atomic_load_n(&runtime->raw_tail, __ATOMIC_ACQUIRE);
    return head - tail;
}

/* The DRDY ISR writes the timestamp as two native-width words bracketed by a
 * sequence lock. This avoids a non-native 64-bit atomic operation in the ISR
 * while ADC_DEBUG still receives a coherent timestamp snapshot. */
static void IRAM_ATTR store_last_drdy_timestamp_isr(ads_runtime_t *runtime,
                                                    uint64_t timestamp_us)
{
    (void)__atomic_add_fetch(&runtime->last_drdy_timestamp_version, 1u,
                             __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->last_drdy_timestamp_low, (uint32_t)timestamp_us,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&runtime->last_drdy_timestamp_high,
                     (uint32_t)(timestamp_us >> 32), __ATOMIC_RELAXED);
    (void)__atomic_add_fetch(&runtime->last_drdy_timestamp_version, 1u,
                             __ATOMIC_RELEASE);
}

static uint64_t load_last_drdy_timestamp(const ads_runtime_t *runtime)
{
    uint32_t first_version;
    uint32_t second_version;
    uint32_t low;
    uint32_t high;
    do {
        first_version = __atomic_load_n(&runtime->last_drdy_timestamp_version,
                                        __ATOMIC_ACQUIRE);
        low = __atomic_load_n(&runtime->last_drdy_timestamp_low, __ATOMIC_RELAXED);
        high = __atomic_load_n(&runtime->last_drdy_timestamp_high, __ATOMIC_RELAXED);
        second_version = __atomic_load_n(&runtime->last_drdy_timestamp_version,
                                         __ATOMIC_ACQUIRE);
    } while ((first_version & 1u) != 0u || first_version != second_version);
    return ((uint64_t)high << 32) | low;
}

/* Only the ADS task calls these after DRDY is disabled and, for raw data,
 * after the DARK consumer has acknowledged a drain. */
static void clear_event_ring(ads_runtime_t *runtime)
{
    const uint32_t head = __atomic_load_n(&runtime->event_head, __ATOMIC_ACQUIRE);
    __atomic_store_n(&runtime->event_tail, head, __ATOMIC_RELEASE);
}

static void clear_raw_ring(ads_runtime_t *runtime)
{
    const uint32_t head = __atomic_load_n(&runtime->raw_head, __ATOMIC_ACQUIRE);
    __atomic_store_n(&runtime->raw_tail, head, __ATOMIC_RELEASE);
}

static bool event_pop(ads_runtime_t *runtime, drdy_event_t *out_event)
{
    const uint32_t tail = __atomic_load_n(&runtime->event_tail, __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&runtime->event_head, __ATOMIC_ACQUIRE);
    if (tail == head) {
        return false;
    }
    *out_event = runtime->event_ring[tail & EVENT_RING_MASK];
    __atomic_store_n(&runtime->event_tail, tail + 1u, __ATOMIC_RELEASE);
    counter_add(&runtime->health->drdy_events_popped, 1u);
    return true;
}

static bool raw_push(ads_runtime_t *runtime, const nocti_v4_raw_sample_t *sample)
{
    const uint32_t head = __atomic_load_n(&runtime->raw_head, __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&runtime->raw_tail, __ATOMIC_ACQUIRE);
    const uint32_t depth = head - tail;
    if (depth >= NOCTI_V4_RAW_SAMPLE_RING_CAPACITY) {
        return false;
    }
    runtime->raw_ring[head & RAW_RING_MASK] = *sample;
    __atomic_store_n(&runtime->raw_head, head + 1u, __ATOMIC_RELEASE);
    update_max((uint32_t *)&runtime->raw_depth_max, depth + 1u);
    return true;
}

static bool raw_pop(ads_runtime_t *runtime, nocti_v4_raw_sample_t *out_sample)
{
    const uint32_t tail = __atomic_load_n(&runtime->raw_tail, __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&runtime->raw_head, __ATOMIC_ACQUIRE);
    if (tail == head) {
        return false;
    }
    *out_sample = runtime->raw_ring[tail & RAW_RING_MASK];
    __atomic_store_n(&runtime->raw_tail, tail + 1u, __ATOMIC_RELEASE);
    return true;
}

static void pack_command(uint16_t command, uint8_t out_word[3])
{
    out_word[0] = (uint8_t)(command >> 8);
    out_word[1] = (uint8_t)command;
    out_word[2] = 0u;
}

static uint32_t unpack_u24(const uint8_t in_word[3])
{
    return ((uint32_t)in_word[0] << 16) | ((uint32_t)in_word[1] << 8) |
           (uint32_t)in_word[2];
}

static int32_t sign_extend_24(uint32_t value)
{
    return (value & 0x800000u) != 0u ? (int32_t)(value | 0xFF000000u) :
                                         (int32_t)value;
}

static uint16_t crc16_ccitt(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;
    for (size_t index = 0u; index < length; ++index) {
        crc ^= (uint16_t)data[index] << 8;
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) != 0u ?
                (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static esp_err_t transfer_frame(ads_transport_t *transport)
{
    memset(transport->rx, 0, sizeof(transport->rx));
    transport->transaction.length = NOCTI_V4_ADS_FRAME_BYTES * 8u;
    transport->transaction.rxlength = NOCTI_V4_ADS_FRAME_BYTES * 8u;
    transport->transaction.tx_buffer = transport->tx;
    transport->transaction.rx_buffer = transport->rx;
    return spi_device_polling_transmit(transport->device, &transport->transaction);
}

static esp_err_t simple_command(ads_transport_t *transport, uint16_t command)
{
    memset(transport->tx, 0, sizeof(transport->tx));
    pack_command(command, &transport->tx[0]);
    return transfer_frame(transport);
}

static esp_err_t read_register(ads_transport_t *transport, uint8_t address,
                               uint16_t *out_value)
{
    if (out_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(transport->tx, 0, sizeof(transport->tx));
    pack_command(ADS_CMD_RREG(address, 0u), &transport->tx[0]);
    esp_err_t err = transfer_frame(transport);
    if (err != ESP_OK) {
        return err;
    }
    memset(transport->tx, 0, sizeof(transport->tx));
    pack_command(ADS_CMD_NULL, &transport->tx[0]);
    err = transfer_frame(transport);
    if (err != ESP_OK) {
        return err;
    }
    *out_value = (uint16_t)(unpack_u24(&transport->rx[0]) >> 8);
    return ESP_OK;
}

static esp_err_t write_register_verified(ads_transport_t *transport, uint8_t address,
                                         uint16_t value)
{
    memset(transport->tx, 0, sizeof(transport->tx));
    pack_command(ADS_CMD_WREG(address, 0u), &transport->tx[0]);
    transport->tx[3] = (uint8_t)(value >> 8);
    transport->tx[4] = (uint8_t)value;
    esp_err_t err = transfer_frame(transport);
    if (err != ESP_OK) {
        return err;
    }
    uint16_t observed = 0u;
    err = read_register(transport, address, &observed);
    if (err != ESP_OK) {
        return err;
    }
    return observed == value ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t transport_init(ads_runtime_t *runtime)
{
    ads_transport_t *transport = &runtime->transport;
    const spi_bus_config_t bus = {
        .mosi_io_num = NOCTI_V4_ADS131_MOSI_GPIO,
        .miso_io_num = NOCTI_V4_ADS131_MISO_GPIO,
        .sclk_io_num = NOCTI_V4_ADS131_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = NOCTI_V4_ADS_FRAME_BYTES,
    };
    esp_err_t err = spi_bus_initialize(NOCTI_V4_ADS_SPI_HOST, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK) {
        return err;
    }
    transport->bus_initialized = true;
    const spi_device_interface_config_t device = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = NOCTI_V4_ADS_SPI_MODE,
        .clock_source = SPI_CLK_SRC_DEFAULT,
        .duty_cycle_pos = 128,
        .cs_ena_pretrans = NOCTI_V4_ADS_SPI_CS_GUARD_CYCLES,
        .cs_ena_posttrans = NOCTI_V4_ADS_SPI_CS_GUARD_CYCLES,
        .clock_speed_hz = NOCTI_V4_ADS_SPI_CLOCK_HZ,
        .input_delay_ns = 0,
        .spics_io_num = NOCTI_V4_ADS131_CS_GPIO,
        .flags = 0,
        .queue_size = NOCTI_V4_ADS_SPI_QUEUE_SIZE,
        .pre_cb = NULL,
        .post_cb = NULL,
    };
    err = spi_bus_add_device(NOCTI_V4_ADS_SPI_HOST, &device, &transport->device);
    if (err != ESP_OK) {
        (void)spi_bus_free(NOCTI_V4_ADS_SPI_HOST);
        transport->bus_initialized = false;
        return err;
    }
    transport->device_added = true;
    memset(&transport->transaction, 0, sizeof(transport->transaction));
    return ESP_OK;
}

static esp_err_t transport_acquire(ads_transport_t *transport)
{
    if (transport == NULL || !transport->device_added || transport->device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (transport->bus_acquired) {
        return ESP_OK;
    }
    const esp_err_t err = spi_device_acquire_bus(transport->device, portMAX_DELAY);
    if (err == ESP_OK) {
        transport->bus_acquired = true;
    }
    return err;
}

static void transport_release(ads_transport_t *transport)
{
    if (transport != NULL && transport->bus_acquired && transport->device != NULL) {
        spi_device_release_bus(transport->device);
        transport->bus_acquired = false;
    }
}

static esp_err_t hardware_reset(ads_runtime_t *runtime)
{
    esp_err_t err = nocti_v4_board_assert_ads131_reset(runtime->board);
    if (err != ESP_OK) {
        return err;
    }
    /* Datasheet minimum reset pulse is 2048 tCLKIN. At the proven 8.192 MHz
     * board clock that is 250 us; V2/V3 retained a documented 300 us pulse. */
    esp_rom_delay_us(300u);
    err = nocti_v4_board_release_ads131_reset(runtime->board);
    if (err != ESP_OK) {
        return err;
    }
    /* V2/V3 retained 20 us before the first response frame. */
    esp_rom_delay_us(20u);
    memset(runtime->transport.tx, 0, sizeof(runtime->transport.tx));
    pack_command(ADS_CMD_NULL, &runtime->transport.tx[0]);
    err = transfer_frame(&runtime->transport);
    if (err != ESP_OK) {
        return err;
    }
    const uint16_t response = (uint16_t)(unpack_u24(&runtime->transport.rx[0]) >> 8);
    return response == ADS_RESET_RESPONSE ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t sync_conversions(ads_runtime_t *runtime)
{
    esp_err_t err = nocti_v4_board_assert_ads131_reset(runtime->board);
    if (err != ESP_OK) {
        return err;
    }
    /* This is 1..2047 tCLKIN (SYNC), not a reset: 5 us is the validated V2/V3 pulse. */
    esp_rom_delay_us(5u);
    return nocti_v4_board_release_ads131_reset(runtime->board);
}

static esp_err_t read_registers(ads_runtime_t *runtime,
                                 nocti_v4_ads_register_snapshot_t *out_regs)
{
    nocti_v4_ads_register_snapshot_t regs = {0};
    esp_err_t err = read_register(&runtime->transport, ADS_REG_ID, &regs.id);
    if (err == ESP_OK) err = read_register(&runtime->transport, ADS_REG_STATUS, &regs.status);
    if (err == ESP_OK) err = read_register(&runtime->transport, ADS_REG_MODE, &regs.mode);
    if (err == ESP_OK) err = read_register(&runtime->transport, ADS_REG_CLOCK, &regs.clock);
    if (err == ESP_OK) err = read_register(&runtime->transport, ADS_REG_GAIN, &regs.gain);
    if (err == ESP_OK) err = read_register(&runtime->transport, ADS_REG_CFG, &regs.cfg);
    for (uint8_t channel = 0u; err == ESP_OK && channel < NOCTI_V4_ADS_CHANNEL_COUNT;
         ++channel) {
        err = read_register(&runtime->transport, ADS_CHANNEL_REG(channel, 0u),
                            &regs.channel_cfg[channel]);
    }
    if (err != ESP_OK) {
        return err;
    }
    runtime->registers = regs;
    if (out_regs != NULL) {
        *out_regs = regs;
    }
    return ESP_OK;
}

static uint32_t calibration_raw24(uint16_t msb, uint16_t lsb)
{
    return ((uint32_t)msb << 8) | ((uint32_t)lsb >> 8);
}

static esp_err_t read_channel_register_block(
    ads_runtime_t *runtime, nocti_v4_ads_channel_register_snapshot_t *out_snapshot)
{
    if (runtime == NULL || out_snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nocti_v4_ads_channel_register_snapshot_t snapshot = {
        .result = ESP_FAIL,
    };
    esp_err_t err = ESP_OK;
    for (uint8_t channel = 0u;
         err == ESP_OK && channel < NOCTI_V4_ADS_CHANNEL_COUNT; ++channel) {
        err = read_register(&runtime->transport, ADS_CHANNEL_REG(channel, 0u),
                            &snapshot.channel_cfg[channel]);
        if (err == ESP_OK) {
            err = read_register(&runtime->transport,
                                ADS_CHANNEL_REG(channel,
                                                ADS_CHANNEL_OCAL_MSB_OFFSET),
                                &snapshot.ocal_msb[channel]);
        }
        if (err == ESP_OK) {
            err = read_register(&runtime->transport,
                                ADS_CHANNEL_REG(channel,
                                                ADS_CHANNEL_OCAL_LSB_OFFSET),
                                &snapshot.ocal_lsb[channel]);
        }
        if (err == ESP_OK) {
            err = read_register(&runtime->transport,
                                ADS_CHANNEL_REG(channel,
                                                ADS_CHANNEL_GCAL_MSB_OFFSET),
                                &snapshot.gcal_msb[channel]);
        }
        if (err == ESP_OK) {
            err = read_register(&runtime->transport,
                                ADS_CHANNEL_REG(channel,
                                                ADS_CHANNEL_GCAL_LSB_OFFSET),
                                &snapshot.gcal_lsb[channel]);
        }
    }
    snapshot.result = err;
    snapshot.valid = err == ESP_OK;
    *out_snapshot = snapshot;
    return err;
}

static esp_err_t configure_8ksps(ads_runtime_t *runtime)
{
    esp_err_t err = simple_command(&runtime->transport, ADS_CMD_UNLOCK);
    if (err != ESP_OK) {
        return err;
    }
    uint16_t mode = 0u;
    err = read_register(&runtime->transport, ADS_REG_MODE, &mode);
    if (err != ESP_OK) {
        return err;
    }
    mode &= (uint16_t)~ADS_MODE_WLENGTH_MASK;
    mode |= ADS_MODE_WLENGTH_24;
    mode &= (uint16_t)~ADS_MODE_RX_CRC_MASK;
    mode &= (uint16_t)~ADS_MODE_RESET_MASK;
    mode &= (uint16_t)~ADS_MODE_CRC_TYPE_MASK;
    mode &= (uint16_t)~ADS_MODE_DRDY_HIZ_MASK;
    mode |= ADS_MODE_DRDY_PULSE;

    err = write_register_verified(&runtime->transport, ADS_REG_MODE, mode);
    if (err == ESP_OK) {
        err = write_register_verified(&runtime->transport, ADS_REG_CLOCK,
                                      NOCTI_V4_ADS_CLOCK_REGISTER_8KSPS);
    }
    if (err == ESP_OK) {
        err = write_register_verified(&runtime->transport, ADS_REG_GAIN,
                                      NOCTI_V4_ADS_GAIN_REGISTER_X1);
    }
    if (err == ESP_OK) {
        err = write_register_verified(&runtime->transport, ADS_REG_CFG,
                                      NOCTI_V4_ADS_CFG_CONTINUOUS);
    }
    for (uint8_t channel = 0u; err == ESP_OK && channel < NOCTI_V4_ADS_CHANNEL_COUNT;
         ++channel) {
        /* The V2/V3 physical configuration uses default zero phase, DC-block
         * disabled, and normal input mux selection on all four channels. */
        err = write_register_verified(&runtime->transport,
                                      ADS_CHANNEL_REG(channel, 0u),
                                      NOCTI_V4_ADS_CHANNEL_CFG_DEFAULT);
    }
    if (err != ESP_OK) {
        return err;
    }
    nocti_v4_ads_register_snapshot_t regs;
    err = read_registers(runtime, &regs);
    if (err != ESP_OK) {
        return err;
    }
    if ((regs.id & ADS_ID_CHANCNT_MASK) != ADS_ID_CHANCNT_4 ||
        (regs.mode & ADS_MODE_WLENGTH_MASK) != ADS_MODE_WLENGTH_24 ||
        (regs.mode & ADS_MODE_RX_CRC_MASK) != 0u ||
        (regs.mode & ADS_MODE_CRC_TYPE_MASK) != 0u ||
        (regs.mode & ADS_MODE_DRDY_HIZ_MASK) != 0u ||
        (regs.mode & ADS_MODE_DRDY_FMT_MASK) != ADS_MODE_DRDY_PULSE ||
        regs.clock != NOCTI_V4_ADS_CLOCK_REGISTER_8KSPS ||
        regs.gain != NOCTI_V4_ADS_GAIN_REGISTER_X1 ||
        regs.cfg != NOCTI_V4_ADS_CFG_CONTINUOUS) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    for (uint8_t channel = 0u; channel < NOCTI_V4_ADS_CHANNEL_COUNT; ++channel) {
        if (regs.channel_cfg[channel] != NOCTI_V4_ADS_CHANNEL_CFG_DEFAULT) {
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    /* Calibration is always active. Hardware reset establishes OCAL=0 and
     * GCAL=0x800000 (unity); V4 never writes these registers during normal
     * setup. Explicitly verify them so a channel cannot silently acquire with
     * zero gain, as the pre-V4-6.4 bad register stride did to CH0. */
    nocti_v4_ads_channel_register_snapshot_t channel_registers;
    err = read_channel_register_block(runtime, &channel_registers);
    if (err != ESP_OK) {
        return err;
    }
    for (uint8_t channel = 0u; channel < NOCTI_V4_ADS_CHANNEL_COUNT; ++channel) {
        if (calibration_raw24(channel_registers.ocal_msb[channel],
                              channel_registers.ocal_lsb[channel]) != 0u ||
            calibration_raw24(channel_registers.gcal_msb[channel],
                              channel_registers.gcal_lsb[channel]) !=
                ADS_GCAL_UNITY_RAW) {
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    runtime->channel_registers = channel_registers;
    return ESP_OK;
}

static void decode_data_frame(const uint8_t raw[NOCTI_V4_ADS_FRAME_BYTES],
                              ads_frame_t *out_frame)
{
    ads_frame_t frame = {0};
    frame.status = (uint16_t)(unpack_u24(&raw[ADS_FRAME_STATUS_OFFSET]) >> 8);
    for (uint8_t channel = 0u; channel < NOCTI_V4_ADS_CHANNEL_COUNT; ++channel) {
        frame.channel[channel] = sign_extend_24(unpack_u24(
            &raw[ADS_FRAME_CH0_OFFSET + channel * NOCTI_V4_ADS_WORD_BYTES]));
    }
    frame.crc_received = (uint16_t)(unpack_u24(&raw[ADS_FRAME_CRC_OFFSET]) >> 8);
    frame.crc_valid = frame.crc_received == crc16_ccitt(raw, ADS_FRAME_CRC_OFFSET);
    frame.resync_occurred = (frame.status & ADS_STATUS_F_RESYNC_MASK) != 0u;
    frame.status_valid = (frame.status & ADS_MODE_WLENGTH_MASK) == ADS_MODE_WLENGTH_24 &&
                         (frame.status & ADS_STATUS_CRC_TYPE_MASK) == 0u &&
                         (frame.status & ADS_STATUS_HARD_FAULT_MASK) == 0u;
    *out_frame = frame;
}

static bool decoder_self_test(void)
{
    uint8_t raw[NOCTI_V4_ADS_FRAME_BYTES] = {
        0x01u, 0x00u, 0x00u, /* valid 24-bit-word STATUS */
        0x12u, 0x34u, 0x56u, /* CH0 positive */
        0xFFu, 0xFFu, 0xFFu, /* CH1 = -1 */
        0x80u, 0x00u, 0x00u, /* CH2 minimum */
        0x7Fu, 0xFFu, 0xFFu, /* CH3 maximum */
        0x00u, 0x00u, 0x00u,
    };
    const uint16_t crc = crc16_ccitt(raw, ADS_FRAME_CRC_OFFSET);
    raw[ADS_FRAME_CRC_OFFSET] = (uint8_t)(crc >> 8);
    raw[ADS_FRAME_CRC_OFFSET + 1u] = (uint8_t)crc;
    ads_frame_t frame;
    decode_data_frame(raw, &frame);
    return frame.status == 0x0100u && frame.channel[0] == 0x123456 &&
           frame.channel[1] == -1 && frame.channel[2] == -8388608 &&
           frame.channel[3] == 8388607 && frame.crc_received == crc &&
           frame.crc_valid && frame.status_valid && !frame.resync_occurred;
}

static esp_err_t read_data_frame(ads_runtime_t *runtime, ads_frame_t *out_frame,
                                 uint32_t *out_duration_us, uint64_t *out_complete_us,
                                 uint8_t out_raw[NOCTI_V4_ADS_FRAME_BYTES])
{
    if (out_frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(runtime->transport.tx, 0, sizeof(runtime->transport.tx));
    uint64_t start_us = 0u;
    uint64_t complete_us = 0u;
    (void)nocti_v4_time_now_us(runtime->timebase, &start_us);
    const esp_err_t result = transfer_frame(&runtime->transport);
    (void)nocti_v4_time_now_us(runtime->timebase, &complete_us);
    if (out_duration_us != NULL) {
        *out_duration_us = complete_us >= start_us ?
            (uint32_t)(complete_us - start_us) : 0u;
    }
    if (out_complete_us != NULL) {
        *out_complete_us = complete_us;
    }
    if (result != ESP_OK) {
        return result;
    }
    if (out_raw != NULL) {
        memcpy(out_raw, runtime->transport.rx, NOCTI_V4_ADS_FRAME_BYTES);
    }
    decode_data_frame(runtime->transport.rx, out_frame);
    return ESP_OK;
}

static void record_spi_duration(ads_runtime_t *runtime, uint32_t duration_us)
{
    update_min(&runtime->health->spi_duration_min_us, duration_us);
    update_max(&runtime->health->spi_duration_max_us, duration_us);
    runtime->spi_duration_sum_us += duration_us;
    ++runtime->spi_duration_count;
    counter_store(&runtime->health->spi_duration_avg_us,
                  (uint32_t)(runtime->spi_duration_sum_us /
                             runtime->spi_duration_count));
    if (duration_us > 50u) {
        counter_add(&runtime->health->spi_duration_gt_50_us, 1u);
    }
    if (duration_us > 100u) {
        counter_add(&runtime->health->spi_duration_gt_100_us, 1u);
    }
    if (duration_us > 250u) {
        counter_add(&runtime->health->spi_duration_gt_250_us, 1u);
    }
}

static void record_period(ads_runtime_t *runtime, uint64_t previous_us,
                          uint64_t current_us)
{
    if (previous_us == 0u || current_us <= previous_us) {
        return;
    }
    const uint32_t period_us = (uint32_t)(current_us - previous_us);
    update_min(&runtime->health->adc_period_min_us, period_us);
    update_max(&runtime->health->adc_period_max_us, period_us);
    update_max(&runtime->health->adc_max_gap_us, period_us);
    runtime->period_sum_us += period_us;
    ++runtime->period_count;
    if (runtime->runtime_period_count < NOCTI_V4_ADS_RUNTIME_PERIOD_SNAPSHOT_COUNT) {
        runtime->runtime_period_us[runtime->runtime_period_count++] = period_us;
    }
    if (period_us < 100u) {
        counter_add(&runtime->health->drdy_period_lt_100_us, 1u);
    } else if (period_us <= 150u) {
        counter_add(&runtime->health->drdy_period_100_to_150_us, 1u);
    } else if (period_us <= 250u) {
        counter_add(&runtime->health->drdy_period_150_to_250_us, 1u);
    } else {
        counter_add(&runtime->health->drdy_period_gt_250_us, 1u);
    }
    counter_store(&runtime->health->adc_period_avg_us,
                  (uint32_t)(runtime->period_sum_us / runtime->period_count));
}

static void IRAM_ATTR drdy_isr(void *argument)
{
    ads_runtime_t *runtime = (ads_runtime_t *)argument;
    __atomic_store_n(&runtime->gpio_isr_callback_core, xPortGetCoreID(),
                     __ATOMIC_RELAXED);
    if (!__atomic_load_n(&runtime->capture_enabled, __ATOMIC_RELAXED)) {
        return;
    }
    counter_add(&runtime->health->drdy_isr_count, 1u);
    uint64_t timestamp_us = 0u;
    BaseType_t higher_priority_woken = pdFALSE;
    if (nocti_v4_time_now_us_isr(runtime->timebase, &timestamp_us) != ESP_OK) {
        counter_add(&runtime->health->drdy_isr_timestamp_errors, 1u);
        counter_add(&runtime->health->adc_missed_other, 1u);
        counter_add(&runtime->health->adc_missed, 1u);
        __atomic_store_n(&runtime->isr_capture_fault_code,
                         NOCTI_V4_ADS_FAULT_ISR_TIMESTAMP, __ATOMIC_RELEASE);
        __atomic_store_n(&runtime->event_overflow_pending, true, __ATOMIC_RELEASE);
        __atomic_store_n(&runtime->session_integrity_fault, true, __ATOMIC_RELEASE);
    } else {
        const uint32_t sequence = __atomic_add_fetch(&runtime->drdy_sequence, 1u,
                                                      __ATOMIC_RELAXED);
        store_last_drdy_timestamp_isr(runtime, timestamp_us);
        __atomic_store_n(&runtime->last_drdy_sequence, sequence, __ATOMIC_RELEASE);
        const uint32_t head = __atomic_load_n(&runtime->event_head, __ATOMIC_RELAXED);
        const uint32_t tail = __atomic_load_n(&runtime->event_tail, __ATOMIC_ACQUIRE);
        const uint32_t depth = head - tail;
        if (depth >= NOCTI_V4_DRDY_EVENT_RING_CAPACITY) {
            counter_add(&runtime->health->drdy_event_overflow, 1u);
            counter_add(&runtime->health->adc_missed_other, 1u);
            counter_add(&runtime->health->adc_missed, 1u);
            __atomic_store_n(&runtime->isr_capture_fault_code,
                             NOCTI_V4_ADS_FAULT_EVENT_RING_OVERFLOW,
                             __ATOMIC_RELEASE);
            __atomic_store_n(&runtime->event_overflow_pending, true, __ATOMIC_RELEASE);
            __atomic_store_n(&runtime->session_integrity_fault, true, __ATOMIC_RELEASE);
        } else {
            runtime->event_ring[head & EVENT_RING_MASK] = (drdy_event_t) {
                .timestamp_us = timestamp_us,
                .sequence = sequence,
                .session_epoch = __atomic_load_n(&runtime->session_epoch,
                                                  __ATOMIC_RELAXED),
            };
            __atomic_store_n(&runtime->event_head, head + 1u, __ATOMIC_RELEASE);
            counter_add(&runtime->health->drdy_events_pushed, 1u);
            update_max((uint32_t *)&runtime->event_depth_max, depth + 1u);
        }
    }
    if (runtime->ads_task != NULL) {
        vTaskNotifyGiveFromISR(runtime->ads_task, &higher_priority_woken);
    }
    if (higher_priority_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static esp_err_t install_drdy_interrupt(ads_runtime_t *runtime)
{
    const gpio_config_t input = {
        .pin_bit_mask = 1ULL << NOCTI_V4_ADS131_DRDY_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    esp_err_t err = gpio_config(&input);
    if (err != ESP_OK) {
        return err;
    }
    runtime->gpio_isr_install_core = (uint32_t)xPortGetCoreID();
    err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_isr_handler_add(NOCTI_V4_ADS131_DRDY_GPIO, drdy_isr, runtime);
    if (err != ESP_OK) {
        return err;
    }
    return gpio_intr_disable(NOCTI_V4_ADS131_DRDY_GPIO);
}

static esp_err_t wait_for_event(ads_runtime_t *runtime, drdy_event_t *out_event)
{
    uint64_t start_us = 0u;
    (void)nocti_v4_time_now_us(runtime->timebase, &start_us);
    const uint64_t deadline_us = start_us +
        (uint64_t)NOCTI_V4_ADS_EVENT_WAIT_TIMEOUT_MS * 1000u;
    for (;;) {
        if (event_pop(runtime, out_event)) {
            return ESP_OK;
        }
        uint64_t now_us = 0u;
        (void)nocti_v4_time_now_us(runtime->timebase, &now_us);
        if (now_us >= deadline_us) {
            return ESP_ERR_TIMEOUT;
        }
        (void)ulTaskNotifyTake(pdTRUE,
                               pdMS_TO_TICKS(NOCTI_V4_ADS_EVENT_WAIT_TIMEOUT_MS));
    }
}

static bool validate_frame(const ads_frame_t *frame, uint32_t *resync_budget)
{
    if (!frame->crc_valid || !frame->status_valid) {
        return false;
    }
    if (frame->resync_occurred) {
        if (*resync_budget == 0u) {
            return false;
        }
        --*resync_budget;
    }
    return true;
}

static esp_err_t validate_rate(ads_runtime_t *runtime)
{
    (void)gpio_intr_disable(NOCTI_V4_ADS131_DRDY_GPIO);
    __atomic_store_n(&runtime->capture_enabled, false, __ATOMIC_RELEASE);
    esp_err_t err = sync_conversions(runtime);
    if (err != ESP_OK) {
        return err;
    }
    clear_event_ring(runtime);
    __atomic_store_n(&runtime->drdy_sequence, 0u, __ATOMIC_RELEASE);
    (void)ulTaskNotifyTake(pdTRUE, 0u);
    __atomic_store_n(&runtime->capture_enabled, true, __ATOMIC_RELEASE);
    err = gpio_intr_enable(NOCTI_V4_ADS131_DRDY_GPIO);
    if (err != ESP_OK) {
        __atomic_store_n(&runtime->capture_enabled, false, __ATOMIC_RELEASE);
        return err;
    }

    uint32_t resync_budget = NOCTI_V4_ADS_EXPECTED_RESYNC_BUDGET;
    for (uint32_t index = 0u; index < NOCTI_V4_ADS_SYNC_DISCARD_FRAMES; ++index) {
        drdy_event_t event;
        err = wait_for_event(runtime, &event);
        if (err != ESP_OK) {
            goto done;
        }
        ads_frame_t frame;
        uint32_t duration_us = 0u;
        err = read_data_frame(runtime, &frame, &duration_us, NULL, NULL);
        if (err != ESP_OK || !validate_frame(&frame, &resync_budget)) {
            err = err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err;
            goto done;
        }
    }

    uint64_t first_timestamp_us = 0u;
    uint64_t previous_timestamp_us = 0u;
    uint64_t period_sum_us = 0u;
    uint32_t period_min_us = UINT32_MAX;
    uint32_t period_max_us = 0u;
    uint32_t previous_sequence = 0u;
    uint32_t sequence_gaps = 0u;
    uint32_t crc_bad = 0u;
    uint32_t status_faults = 0u;
    uint32_t spi_errors = 0u;
    for (uint32_t index = 0u; index < NOCTI_V4_ADS_RATE_VALIDATION_SAMPLES; ++index) {
        drdy_event_t event;
        err = wait_for_event(runtime, &event);
        if (err != ESP_OK) {
            goto done;
        }
        if (index == 0u) {
            first_timestamp_us = event.timestamp_us;
        }
        if (previous_sequence != 0u && event.sequence != previous_sequence + 1u) {
            sequence_gaps += event.sequence - previous_sequence - 1u;
        }
        if (previous_timestamp_us != 0u) {
            const uint32_t period_us =
                (uint32_t)(event.timestamp_us - previous_timestamp_us);
            period_sum_us += period_us;
            if (period_us < period_min_us) period_min_us = period_us;
            if (period_us > period_max_us) period_max_us = period_us;
        }
        ads_frame_t frame;
        uint32_t duration_us = 0u;
        const esp_err_t frame_err = read_data_frame(runtime, &frame, &duration_us,
                                                     NULL, NULL);
        if (frame_err != ESP_OK) {
            ++spi_errors;
        } else if (!frame.crc_valid) {
            ++crc_bad;
        } else if (!frame.status_valid || frame.resync_occurred) {
            ++status_faults;
        }
        previous_sequence = event.sequence;
        previous_timestamp_us = event.timestamp_us;
        if (frame_err != ESP_OK) {
            err = frame_err;
            goto done;
        }
    }

    {
        const uint32_t intervals = NOCTI_V4_ADS_RATE_VALIDATION_SAMPLES - 1u;
        const uint64_t elapsed_us = previous_timestamp_us - first_timestamp_us;
        if (elapsed_us == 0u || intervals == 0u) {
            err = ESP_ERR_INVALID_RESPONSE;
            goto done;
        }
        runtime->measured_sps = (uint32_t)(((uint64_t)intervals * 1000000u +
                                            elapsed_us / 2u) / elapsed_us);
        runtime->validation_period_avg_us =
            (uint32_t)((period_sum_us + intervals / 2u) / intervals);
        runtime->validation_period_min_us = period_min_us == UINT32_MAX ? 0u : period_min_us;
        runtime->validation_period_max_us = period_max_us;
        runtime->validation_samples = NOCTI_V4_ADS_RATE_VALIDATION_SAMPLES;
        const uint32_t tolerance = NOCTI_V4_ADS_SAMPLE_RATE_HZ *
                                   NOCTI_V4_ADS_RATE_TOLERANCE_PERCENT / 100u;
        runtime->rate_valid = runtime->measured_sps >= NOCTI_V4_ADS_SAMPLE_RATE_HZ - tolerance &&
                              runtime->measured_sps <= NOCTI_V4_ADS_SAMPLE_RATE_HZ + tolerance &&
                              sequence_gaps == 0u && crc_bad == 0u &&
                              status_faults == 0u && spi_errors == 0u &&
                              __atomic_load_n(&runtime->health->drdy_event_overflow,
                                              __ATOMIC_ACQUIRE) == 0u;
        if (!runtime->rate_valid) {
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }

done:
    __atomic_store_n(&runtime->capture_enabled, false, __ATOMIC_RELEASE);
    (void)gpio_intr_disable(NOCTI_V4_ADS131_DRDY_GPIO);
    clear_event_ring(runtime);
    return err;
}

static void reset_acquisition_counters(ads_runtime_t *runtime)
{
#define RESET_ACQ(field) counter_store(&runtime->health->field, 0u)
    RESET_ACQ(adc_ok);
    RESET_ACQ(adc_missed);
    RESET_ACQ(adc_missed_sequence);
    RESET_ACQ(adc_missed_backlog);
    RESET_ACQ(adc_missed_other);
    RESET_ACQ(adc_sequence_gap);
    RESET_ACQ(drdy_isr_count);
    RESET_ACQ(drdy_events_pushed);
    RESET_ACQ(drdy_events_popped);
    RESET_ACQ(drdy_isr_timestamp_errors);
    RESET_ACQ(drdy_event_overflow);
    RESET_ACQ(adc_ring_overflow);
    RESET_ACQ(ads_frames_read_total);
    RESET_ACQ(ads_frames_read_runtime);
    RESET_ACQ(post_sync_frames_read);
    RESET_ACQ(post_sync_frames_discarded);
    RESET_ACQ(raw_samples_pushed);
    RESET_ACQ(crc_bad);
    RESET_ACQ(spi_errors);
    RESET_ACQ(adc_period_min_us);
    RESET_ACQ(adc_period_max_us);
    RESET_ACQ(adc_period_avg_us);
    RESET_ACQ(adc_max_gap_us);
    RESET_ACQ(drdy_period_lt_100_us);
    RESET_ACQ(drdy_period_100_to_150_us);
    RESET_ACQ(drdy_period_150_to_250_us);
    RESET_ACQ(drdy_period_gt_250_us);
    RESET_ACQ(spi_duration_min_us);
    RESET_ACQ(spi_duration_max_us);
    RESET_ACQ(spi_duration_avg_us);
    RESET_ACQ(spi_duration_gt_50_us);
    RESET_ACQ(spi_duration_gt_100_us);
    RESET_ACQ(spi_duration_gt_250_us);
    RESET_ACQ(ads_status_faults);
    RESET_ACQ(unexpected_f_resync);
    RESET_ACQ(raw_samples_consumed);
#undef RESET_ACQ
    runtime->period_sum_us = 0u;
    runtime->period_count = 0u;
    runtime->spi_duration_sum_us = 0u;
    runtime->spi_duration_count = 0u;
    runtime->runtime_period_count = 0u;
    memset(runtime->runtime_period_us, 0, sizeof(runtime->runtime_period_us));
    __atomic_store_n(&runtime->event_depth_max, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->raw_depth_max, 0u, __ATOMIC_RELEASE);
    reset_raw_stats(runtime);
}

static void set_fault(ads_runtime_t *runtime, nocti_v4_ads_fault_code_t code,
                      nocti_v4_ads_fault_origin_t origin, esp_err_t error)
{
    const nocti_v4_ads_lifecycle_t previous = runtime->lifecycle;
    if (previous != NOCTI_V4_ADS_FAULTED) {
        runtime->last_error = error;
        runtime->last_fault_code = code;
        runtime->last_fault_origin = origin;
        runtime->last_fault_error = error;
    }
    __atomic_store_n(&runtime->session_integrity_fault, true, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->capture_enabled, false, __ATOMIC_RELEASE);
    (void)gpio_intr_disable(NOCTI_V4_ADS131_DRDY_GPIO);
    runtime->lifecycle = NOCTI_V4_ADS_FAULTED;
    if (previous != NOCTI_V4_ADS_FAULTED && previous != NOCTI_V4_ADS_STOPPED) {
        counter_add(&runtime->health->dark_session_faults, 1u);
    }
    transport_release(&runtime->transport);
}

static void process_event(ads_runtime_t *runtime, const drdy_event_t *event);

static esp_err_t start_dark_internal(ads_runtime_t *runtime)
{
    if (!runtime->ready || !runtime->rate_valid ||
        runtime->lifecycle != NOCTI_V4_ADS_STOPPED || raw_depth(runtime) != 0u) {
        return ESP_ERR_INVALID_STATE;
    }
    runtime->lifecycle = NOCTI_V4_ADS_STARTING_DARK;
    reset_acquisition_counters(runtime);
    __atomic_store_n(&runtime->session_integrity_fault, false, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->event_overflow_pending, false, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->isr_capture_fault_code, NOCTI_V4_ADS_FAULT_NONE,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->drdy_sequence, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->last_drdy_sequence, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->last_drdy_timestamp_version, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->last_drdy_timestamp_low, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->last_drdy_timestamp_high, 0u, __ATOMIC_RELEASE);
    (void)__atomic_add_fetch(&runtime->session_epoch, 1u, __ATOMIC_ACQ_REL);
    runtime->last_sequence = 0u;
    runtime->last_timestamp_us = 0u;
    runtime->last_error = ESP_OK;
    runtime->last_fault_code = NOCTI_V4_ADS_FAULT_NONE;
    runtime->last_fault_origin = NOCTI_V4_ADS_FAULT_ORIGIN_NONE;
    runtime->last_fault_error = ESP_OK;
    runtime->sync_discard_remaining = NOCTI_V4_ADS_SYNC_DISCARD_FRAMES;
    runtime->expected_resync_remaining = NOCTI_V4_ADS_EXPECTED_RESYNC_BUDGET;
    __atomic_store_n(&runtime->capture_enabled, false, __ATOMIC_RELEASE);
    (void)gpio_intr_disable(NOCTI_V4_ADS131_DRDY_GPIO);
    clear_event_ring(runtime);
    const esp_err_t acquire_err = transport_acquire(&runtime->transport);
    if (acquire_err != ESP_OK) {
        set_fault(runtime, NOCTI_V4_ADS_FAULT_START_BUS_ACQUIRE,
                  NOCTI_V4_ADS_FAULT_ORIGIN_START_DARK, acquire_err);
        return acquire_err;
    }
    const esp_err_t sync_err = sync_conversions(runtime);
    if (sync_err != ESP_OK) {
        set_fault(runtime, NOCTI_V4_ADS_FAULT_START_SYNC,
                  NOCTI_V4_ADS_FAULT_ORIGIN_START_DARK, sync_err);
        return sync_err;
    }
    (void)ulTaskNotifyTake(pdTRUE, 0u);
    uint64_t now_us = 0u;
    (void)nocti_v4_time_now_us(runtime->timebase, &now_us);
    runtime->run_start_timestamp_us = now_us;
    __atomic_store_n(&runtime->capture_enabled, true, __ATOMIC_RELEASE);
    const esp_err_t enable_err = gpio_intr_enable(NOCTI_V4_ADS131_DRDY_GPIO);
    if (enable_err != ESP_OK) {
        set_fault(runtime, NOCTI_V4_ADS_FAULT_START_INTERRUPT,
                  NOCTI_V4_ADS_FAULT_ORIGIN_START_DARK, enable_err);
        return enable_err;
    }

    /* In pulse-mode DRDY, a post-SYNC discard still has to service the ADC.
     * Complete this bounded startup handoff in the ADS task itself. The prior
     * deferred handoff returned through console/logging before the first frame
     * was read, exposing the two-entry FIFO to an avoidable backlog. */
    for (uint32_t index = 0u; index < NOCTI_V4_ADS_SYNC_DISCARD_FRAMES; ++index) {
        drdy_event_t event;
        const esp_err_t wait_err = wait_for_event(runtime, &event);
        if (wait_err != ESP_OK) {
            set_fault(runtime, NOCTI_V4_ADS_FAULT_POST_SYNC_TIMEOUT,
                      NOCTI_V4_ADS_FAULT_ORIGIN_START_DARK, wait_err);
            return wait_err;
        }
        process_event(runtime, &event);
        if (runtime->lifecycle == NOCTI_V4_ADS_FAULTED) {
            return runtime->last_error;
        }
    }
    if (runtime->lifecycle != NOCTI_V4_ADS_RUNNING_DARK) {
        set_fault(runtime, NOCTI_V4_ADS_FAULT_POST_SYNC_STATE,
                  NOCTI_V4_ADS_FAULT_ORIGIN_START_DARK, ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static esp_err_t stop_dark_internal(ads_runtime_t *runtime)
{
    if (runtime->lifecycle == NOCTI_V4_ADS_STOPPED) {
        return ESP_OK;
    }
    const bool fault_history = runtime->lifecycle == NOCTI_V4_ADS_FAULTED;
    if (!fault_history) {
        runtime->lifecycle = NOCTI_V4_ADS_STOPPING_DARK;
    }
    __atomic_store_n(&runtime->capture_enabled, false, __ATOMIC_RELEASE);
    (void)gpio_intr_disable(NOCTI_V4_ADS131_DRDY_GPIO);
    clear_event_ring(runtime);
    __atomic_store_n(&runtime->event_overflow_pending, false, __ATOMIC_RELEASE);
    (void)xSemaphoreTake(runtime->drain_done, 0u);
    __atomic_store_n(&runtime->drain_requested, true, __ATOMIC_RELEASE);
    TaskHandle_t const consumer = __atomic_load_n(&runtime->raw_consumer_task,
                                                  __ATOMIC_ACQUIRE);
    if (consumer != NULL) {
        xTaskNotifyGive(consumer);
    }
    if (xSemaphoreTake(runtime->drain_done,
                       pdMS_TO_TICKS(NOCTI_V4_ADS_CONTROL_TIMEOUT_MS)) != pdTRUE) {
        if (!fault_history) {
            set_fault(runtime, NOCTI_V4_ADS_FAULT_STOP_DRAIN_TIMEOUT,
                      NOCTI_V4_ADS_FAULT_ORIGIN_STOP_DARK, ESP_ERR_TIMEOUT);
        }
        return ESP_ERR_TIMEOUT;
    }
    clear_raw_ring(runtime);
    transport_release(&runtime->transport);
    runtime->lifecycle = NOCTI_V4_ADS_STOPPED;
    counter_add(&runtime->health->dark_sessions_stopped, 1u);
    return ESP_OK;
}

static void populate_frame_record(nocti_v4_ads_frame_dump_record_t *record,
                                  const drdy_event_t *event,
                                  const ads_frame_t *frame,
                                  uint32_t duration_us)
{
    for (uint8_t word = 0u; word < NOCTI_V4_ADS_FRAME_WORDS; ++word) {
        record->raw_word[word] = unpack_u24(
            &record->raw[word * NOCTI_V4_ADS_WORD_BYTES]);
    }
    record->drdy_timestamp_us = event->timestamp_us;
    record->drdy_sequence = event->sequence;
    record->spi_duration_us = duration_us;
    record->status = frame->status;
    memcpy(record->channel, frame->channel, sizeof(record->channel));
    record->crc_received = frame->crc_received;
    record->crc_calculated = crc16_ccitt(record->raw, ADS_FRAME_CRC_OFFSET);
    record->crc_valid = frame->crc_valid;
    record->status_valid = frame->status_valid;
    record->resync_occurred = frame->resync_occurred;
    record->valid = frame->crc_valid && frame->status_valid &&
                    !frame->resync_occurred;
}

static esp_err_t capture_frame_dump_internal(ads_runtime_t *runtime,
                                             uint8_t frame_count)
{
    if (!runtime->ready || !runtime->rate_valid ||
        runtime->lifecycle != NOCTI_V4_ADS_STOPPED ||
        __atomic_load_n(&runtime->capture_enabled, __ATOMIC_ACQUIRE) ||
        raw_depth(runtime) != 0u || frame_count == 0u ||
        frame_count > NOCTI_V4_ADS_FRAME_DUMP_MAX_FRAMES) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&runtime->frame_dump, 0, sizeof(runtime->frame_dump));
    runtime->frame_dump.requested = frame_count;
    runtime->frame_dump.result = ESP_FAIL;
    esp_err_t err = transport_acquire(&runtime->transport);
    if (err != ESP_OK) {
        runtime->frame_dump.result = err;
        return err;
    }

    (void)gpio_intr_disable(NOCTI_V4_ADS131_DRDY_GPIO);
    __atomic_store_n(&runtime->capture_enabled, false, __ATOMIC_RELEASE);
    clear_event_ring(runtime);
    __atomic_store_n(&runtime->event_overflow_pending, false, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->drdy_sequence, 0u, __ATOMIC_RELEASE);
    (void)ulTaskNotifyTake(pdTRUE, 0u);
    err = sync_conversions(runtime);
    if (err != ESP_OK) {
        goto done;
    }
    __atomic_store_n(&runtime->capture_enabled, true, __ATOMIC_RELEASE);
    err = gpio_intr_enable(NOCTI_V4_ADS131_DRDY_GPIO);
    if (err != ESP_OK) {
        __atomic_store_n(&runtime->capture_enabled, false, __ATOMIC_RELEASE);
        goto done;
    }

    /* DRDY_FMT=1 still requires each scientific discard to be a complete
     * physical 18-byte read. F_RESYNC is accepted only in this bounded
     * post-SYNC window, exactly as in normal acquisition startup. */
    uint32_t resync_budget = NOCTI_V4_ADS_EXPECTED_RESYNC_BUDGET;
    for (uint32_t index = 0u; index < NOCTI_V4_ADS_SYNC_DISCARD_FRAMES; ++index) {
        drdy_event_t event;
        ads_frame_t frame;
        uint32_t duration_us = 0u;
        err = wait_for_event(runtime, &event);
        if (err == ESP_OK) {
            err = read_data_frame(runtime, &frame, &duration_us, NULL, NULL);
        }
        if (err != ESP_OK || !validate_frame(&frame, &resync_budget)) {
            if (err == ESP_OK) err = ESP_ERR_INVALID_RESPONSE;
            goto done;
        }
    }

    for (uint8_t index = 0u; index < frame_count; ++index) {
        drdy_event_t event;
        ads_frame_t frame;
        uint32_t duration_us = 0u;
        nocti_v4_ads_frame_dump_record_t *record = &runtime->frame_dump.frame[index];
        err = wait_for_event(runtime, &event);
        if (err == ESP_OK) {
            err = read_data_frame(runtime, &frame, &duration_us, NULL, record->raw);
        }
        if (err != ESP_OK) {
            goto done;
        }
        populate_frame_record(record, &event, &frame, duration_us);
        ++runtime->frame_dump.captured;
        if (!record->valid) {
            err = ESP_ERR_INVALID_RESPONSE;
            goto done;
        }
    }

done:
    __atomic_store_n(&runtime->capture_enabled, false, __ATOMIC_RELEASE);
    (void)gpio_intr_disable(NOCTI_V4_ADS131_DRDY_GPIO);
    clear_event_ring(runtime);
    (void)ulTaskNotifyTake(pdTRUE, 0u);
    transport_release(&runtime->transport);
    runtime->frame_dump.result = err;
    runtime->frame_dump.valid = err == ESP_OK &&
                                runtime->frame_dump.captured == frame_count;
    return err;
}

static esp_err_t capture_channel_registers_internal(ads_runtime_t *runtime)
{
    if (!runtime->ready || runtime->lifecycle != NOCTI_V4_ADS_STOPPED ||
        __atomic_load_n(&runtime->capture_enabled, __ATOMIC_ACQUIRE) ||
        raw_depth(runtime) != 0u) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = transport_acquire(&runtime->transport);
    if (err == ESP_OK) {
        err = read_channel_register_block(runtime, &runtime->channel_registers);
    }
    transport_release(&runtime->transport);
    return err;
}

static esp_err_t capture_ch0_selftest_internal(ads_runtime_t *runtime)
{
    if (!runtime->ready || !runtime->rate_valid ||
        runtime->lifecycle != NOCTI_V4_ADS_STOPPED ||
        __atomic_load_n(&runtime->capture_enabled, __ATOMIC_ACQUIRE) ||
        raw_depth(runtime) != 0u) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&runtime->ch0_selftest, 0, sizeof(runtime->ch0_selftest));
    nocti_v4_ads_ch0_selftest_snapshot_t *snapshot = &runtime->ch0_selftest;
    snapshot->samples_requested = NOCTI_V4_ADS_CH0_SELFTEST_SAMPLES;
    snapshot->result = ESP_FAIL;
    snapshot->restore_result = ESP_FAIL;

    bool original_known = false;
    esp_err_t err = transport_acquire(&runtime->transport);
    if (err != ESP_OK) {
        snapshot->result = err;
        return err;
    }

    (void)gpio_intr_disable(NOCTI_V4_ADS131_DRDY_GPIO);
    __atomic_store_n(&runtime->capture_enabled, false, __ATOMIC_RELEASE);
    clear_event_ring(runtime);
    (void)ulTaskNotifyTake(pdTRUE, 0u);

    err = read_register(&runtime->transport, ADS_CHANNEL_REG(0u, 0u),
                        &snapshot->original_ch0_cfg);
    if (err != ESP_OK) {
        goto restore;
    }
    original_known = true;
    snapshot->test_ch0_cfg =
        (uint16_t)((snapshot->original_ch0_cfg &
                    (uint16_t)~ADS_CH_CFG_MUX_MASK) |
                   ADS_CH_CFG_MUX_POSITIVE_DC);

    /* Normal initialization leaves the SPI interface unlocked. Repeat the
     * idempotent UNLOCK command here so the stopped-only diagnostic cannot
     * depend on an implicit historical lock state. */
    err = simple_command(&runtime->transport, ADS_CMD_UNLOCK);
    if (err == ESP_OK) {
        err = write_register_verified(&runtime->transport,
                                      ADS_CHANNEL_REG(0u, 0u),
                                      snapshot->test_ch0_cfg);
    }
    if (err != ESP_OK) {
        goto restore;
    }

    __atomic_store_n(&runtime->drdy_sequence, 0u, __ATOMIC_RELEASE);
    err = sync_conversions(runtime);
    if (err != ESP_OK) {
        goto restore;
    }
    __atomic_store_n(&runtime->capture_enabled, true, __ATOMIC_RELEASE);
    err = gpio_intr_enable(NOCTI_V4_ADS131_DRDY_GPIO);
    if (err != ESP_OK) {
        __atomic_store_n(&runtime->capture_enabled, false, __ATOMIC_RELEASE);
        goto restore;
    }

    uint32_t resync_budget = NOCTI_V4_ADS_EXPECTED_RESYNC_BUDGET;
    for (uint32_t index = 0u; index < NOCTI_V4_ADS_SYNC_DISCARD_FRAMES; ++index) {
        drdy_event_t event;
        ads_frame_t frame;
        uint32_t duration_us = 0u;
        err = wait_for_event(runtime, &event);
        if (err == ESP_OK) {
            err = read_data_frame(runtime, &frame, &duration_us, NULL, NULL);
        }
        if (err != ESP_OK || !validate_frame(&frame, &resync_budget)) {
            if (err == ESP_OK) err = ESP_ERR_INVALID_RESPONSE;
            goto restore;
        }
    }

    int64_t sum = 0;
    for (uint32_t index = 0u;
         index < NOCTI_V4_ADS_CH0_SELFTEST_SAMPLES; ++index) {
        drdy_event_t event;
        ads_frame_t frame;
        uint32_t duration_us = 0u;
        nocti_v4_ads_frame_dump_record_t *record =
            index < NOCTI_V4_ADS_FRAME_DUMP_MAX_FRAMES ?
                &snapshot->frame[index] : NULL;
        err = wait_for_event(runtime, &event);
        if (err == ESP_OK) {
            err = read_data_frame(runtime, &frame, &duration_us, NULL,
                                  record != NULL ? record->raw : NULL);
        }
        if (err != ESP_OK || !frame.crc_valid || !frame.status_valid ||
            frame.resync_occurred) {
            if (err == ESP_OK) err = ESP_ERR_INVALID_RESPONSE;
            goto restore;
        }
        if (record != NULL) {
            populate_frame_record(record, &event, &frame, duration_us);
        }
        const int32_t value = frame.channel[0];
        if (snapshot->samples_captured == 0u) {
            snapshot->minimum = value;
            snapshot->maximum = value;
        } else {
            if (value < snapshot->minimum) snapshot->minimum = value;
            if (value > snapshot->maximum) snapshot->maximum = value;
        }
        sum += value;
        ++snapshot->samples_captured;
        ++snapshot->crc_valid_frames;
        ++snapshot->status_valid_frames;
        if ((frame.status & ADS_STATUS_DRDY0_MASK) != 0u) {
            ++snapshot->drdy0_new_data_frames;
        }
    }
    snapshot->mean = sum / (int64_t)snapshot->samples_captured;
    snapshot->signal_nonzero = snapshot->minimum != 0 ||
                               snapshot->maximum != 0;

restore:
    __atomic_store_n(&runtime->capture_enabled, false, __ATOMIC_RELEASE);
    (void)gpio_intr_disable(NOCTI_V4_ADS131_DRDY_GPIO);
    clear_event_ring(runtime);
    (void)ulTaskNotifyTake(pdTRUE, 0u);

    esp_err_t restore_err = original_known ?
        write_register_verified(&runtime->transport,
                                ADS_CHANNEL_REG(0u, 0u),
                                snapshot->original_ch0_cfg) :
        ESP_OK;
    if (restore_err == ESP_OK) {
        restore_err = read_register(&runtime->transport,
                                    ADS_CHANNEL_REG(0u, 0u),
                                    &snapshot->restored_ch0_cfg);
    }
    snapshot->restore_result = restore_err;
    snapshot->restore_verified = original_known && restore_err == ESP_OK &&
        snapshot->restored_ch0_cfg == snapshot->original_ch0_cfg;
    if (snapshot->restore_verified) {
        runtime->registers.channel_cfg[0] = snapshot->restored_ch0_cfg;
    }
    transport_release(&runtime->transport);

    if (original_known && !snapshot->restore_verified) {
        err = restore_err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : restore_err;
    }
    snapshot->result = err;
    snapshot->valid = err == ESP_OK && snapshot->restore_verified &&
                      snapshot->samples_captured ==
                          NOCTI_V4_ADS_CH0_SELFTEST_SAMPLES;
    return err;
}

static void process_event(ads_runtime_t *runtime, const drdy_event_t *event)
{
    const uint32_t backlog = event_depth(runtime);
    if (backlog >= NOCTI_V4_ADS_MAX_EVENT_BACKLOG) {
        /* This is not a temporary queue-depth statistic. Two events remain
         * after the current event was popped, which exceeds the bounded FIFO
         * integrity contract; do not manufacture aligned samples. */
        counter_add(&runtime->health->adc_missed_backlog, backlog);
        counter_add(&runtime->health->adc_missed, backlog);
        set_fault(runtime, NOCTI_V4_ADS_FAULT_EVENT_BACKLOG,
                  NOCTI_V4_ADS_FAULT_ORIGIN_PROCESS_EVENT, ESP_ERR_INVALID_STATE);
        return;
    }
    if (event->session_epoch != __atomic_load_n(&runtime->session_epoch, __ATOMIC_ACQUIRE)) {
        set_fault(runtime, NOCTI_V4_ADS_FAULT_EVENT_EPOCH,
                  NOCTI_V4_ADS_FAULT_ORIGIN_PROCESS_EVENT, ESP_ERR_INVALID_STATE);
        return;
    }
    if (runtime->last_sequence != 0u && event->sequence != runtime->last_sequence + 1u) {
        const uint32_t gap = event->sequence - runtime->last_sequence - 1u;
        counter_add(&runtime->health->adc_sequence_gap, gap);
        counter_add(&runtime->health->adc_missed_sequence, gap);
        counter_add(&runtime->health->adc_missed, gap);
        set_fault(runtime, NOCTI_V4_ADS_FAULT_SEQUENCE_GAP,
                  NOCTI_V4_ADS_FAULT_ORIGIN_PROCESS_EVENT, ESP_ERR_INVALID_STATE);
        return;
    }
    record_period(runtime, runtime->last_timestamp_us, event->timestamp_us);
    runtime->last_sequence = event->sequence;
    runtime->last_timestamp_us = event->timestamp_us;

    ads_frame_t frame;
    uint32_t duration_us = 0u;
    uint64_t complete_us = event->timestamp_us;
    const esp_err_t spi_result = read_data_frame(runtime, &frame, &duration_us,
                                                  &complete_us, NULL);
    record_spi_duration(runtime, duration_us);
    if (spi_result != ESP_OK) {
        counter_add(&runtime->health->spi_errors, 1u);
        set_fault(runtime, NOCTI_V4_ADS_FAULT_SPI_READ,
                  NOCTI_V4_ADS_FAULT_ORIGIN_PROCESS_EVENT, spi_result);
        return;
    }
    counter_add(&runtime->health->ads_frames_read_total, 1u);
    counter_add(&runtime->health->ads_frames_read_runtime, 1u);
    if (runtime->lifecycle == NOCTI_V4_ADS_STARTING_DARK) {
        counter_add(&runtime->health->post_sync_frames_read, 1u);
    }
    if (!frame.crc_valid) {
        counter_add(&runtime->health->crc_bad, 1u);
        set_fault(runtime, NOCTI_V4_ADS_FAULT_CRC,
                  NOCTI_V4_ADS_FAULT_ORIGIN_PROCESS_EVENT, ESP_ERR_INVALID_RESPONSE);
        return;
    }
    if (!frame.status_valid) {
        counter_add(&runtime->health->ads_status_faults, 1u);
        set_fault(runtime, NOCTI_V4_ADS_FAULT_STATUS,
                  NOCTI_V4_ADS_FAULT_ORIGIN_PROCESS_EVENT, ESP_ERR_INVALID_RESPONSE);
        return;
    }
    if (frame.resync_occurred) {
        if (runtime->lifecycle == NOCTI_V4_ADS_STARTING_DARK &&
            runtime->expected_resync_remaining > 0u) {
            --runtime->expected_resync_remaining;
        } else {
            counter_add(&runtime->health->unexpected_f_resync, 1u);
            counter_add(&runtime->health->ads_status_faults, 1u);
            set_fault(runtime, NOCTI_V4_ADS_FAULT_F_RESYNC,
                      NOCTI_V4_ADS_FAULT_ORIGIN_PROCESS_EVENT,
                      ESP_ERR_INVALID_RESPONSE);
            return;
        }
    }
    if (runtime->lifecycle == NOCTI_V4_ADS_STARTING_DARK) {
        if (runtime->sync_discard_remaining == 0u) {
            set_fault(runtime, NOCTI_V4_ADS_FAULT_POST_SYNC_STATE,
                      NOCTI_V4_ADS_FAULT_ORIGIN_PROCESS_EVENT, ESP_ERR_INVALID_STATE);
            return;
        }
        --runtime->sync_discard_remaining;
        counter_add(&runtime->health->post_sync_frames_discarded, 1u);
        if (runtime->sync_discard_remaining == 0u) {
            runtime->expected_resync_remaining = 0u;
            runtime->lifecycle = NOCTI_V4_ADS_RUNNING_DARK;
            counter_add(&runtime->health->dark_sessions_started, 1u);
        }
        return;
    }
    if (runtime->lifecycle != NOCTI_V4_ADS_RUNNING_DARK) {
        return;
    }
    const nocti_v4_raw_sample_t sample = {
        .drdy_timestamp_us = event->timestamp_us,
        .spi_complete_timestamp_us = complete_us,
        .drdy_sequence = event->sequence,
        .ads_status = frame.status,
        .channel = {frame.channel[0], frame.channel[1], frame.channel[2], frame.channel[3]},
        .crc_received = frame.crc_received,
        .crc_valid = true,
        .status_valid = true,
        .spi_result = ESP_OK,
        .session_epoch = event->session_epoch,
    };
    if (!raw_push(runtime, &sample)) {
        counter_add(&runtime->health->adc_ring_overflow, 1u);
        counter_add(&runtime->health->adc_missed_other, 1u);
        counter_add(&runtime->health->adc_missed, 1u);
        set_fault(runtime, NOCTI_V4_ADS_FAULT_RAW_RING_OVERFLOW,
                  NOCTI_V4_ADS_FAULT_ORIGIN_PROCESS_EVENT, ESP_ERR_NO_MEM);
        return;
    }
    record_raw_stats(runtime, &sample);
    counter_add(&runtime->health->raw_samples_pushed, 1u);
    counter_add(&runtime->health->adc_ok, 1u);
    TaskHandle_t const consumer = __atomic_load_n(&runtime->raw_consumer_task,
                                                  __ATOMIC_ACQUIRE);
    if (consumer != NULL) {
        xTaskNotifyGive(consumer);
    }
}

static void process_runtime_events(ads_runtime_t *runtime)
{
    if (__atomic_exchange_n(&runtime->event_overflow_pending, false,
                            __ATOMIC_ACQ_REL)) {
        const nocti_v4_ads_fault_code_t code = __atomic_load_n(
            &runtime->isr_capture_fault_code, __ATOMIC_ACQUIRE);
        set_fault(runtime, code == NOCTI_V4_ADS_FAULT_NONE ?
                      NOCTI_V4_ADS_FAULT_EVENT_RING_OVERFLOW : code,
                  NOCTI_V4_ADS_FAULT_ORIGIN_DRDY_ISR, ESP_ERR_INVALID_STATE);
        return;
    }
    drdy_event_t event;
    while (__atomic_load_n(&runtime->capture_enabled, __ATOMIC_ACQUIRE) &&
           event_pop(runtime, &event)) {
        process_event(runtime, &event);
        if (runtime->lifecycle == NOCTI_V4_ADS_FAULTED) {
            return;
        }
    }
}

static void check_drdy_stall(ads_runtime_t *runtime)
{
    if (!__atomic_load_n(&runtime->capture_enabled, __ATOMIC_ACQUIRE)) {
        return;
    }
    uint64_t now_us = 0u;
    if (nocti_v4_time_now_us(runtime->timebase, &now_us) != ESP_OK) {
        return;
    }
    const uint64_t reference_us = runtime->last_timestamp_us != 0u ?
        runtime->last_timestamp_us : runtime->run_start_timestamp_us;
    if (reference_us != 0u && now_us > reference_us &&
        now_us - reference_us > NOCTI_V4_ADS_DRDY_STALL_TIMEOUT_US) {
        set_fault(runtime, NOCTI_V4_ADS_FAULT_DRDY_STALL,
                  NOCTI_V4_ADS_FAULT_ORIGIN_DRDY_WATCHDOG, ESP_ERR_TIMEOUT);
    }
}

static void dark_consumer_task(void *argument)
{
    ads_runtime_t *runtime = (ads_runtime_t *)argument;
    runtime->dark_task_core = xPortGetCoreID();
    __atomic_store_n(&runtime->health->dark_consumer_task_core,
                     runtime->dark_task_core, __ATOMIC_RELEASE);
    for (;;) {
        nocti_v4_raw_sample_t sample;
        if (__atomic_load_n(&runtime->raw_consumer_mode, __ATOMIC_ACQUIRE) ==
            NOCTI_V4_ADS_RAW_CONSUMER_DARK_DIAGNOSTIC) {
            while (raw_pop(runtime, &sample)) {
                counter_add(&runtime->health->raw_samples_consumed, 1u);
            }
        }
        if (__atomic_load_n(&runtime->drain_requested, __ATOMIC_ACQUIRE) &&
            __atomic_load_n(&runtime->raw_consumer_mode, __ATOMIC_ACQUIRE) ==
                NOCTI_V4_ADS_RAW_CONSUMER_DARK_DIAGNOSTIC &&
            raw_depth(runtime) == 0u) {
            __atomic_store_n(&runtime->drain_requested, false, __ATOMIC_RELEASE);
            (void)xSemaphoreGive(runtime->drain_done);
        }
        /* The raw ring remains the source of samples; notification is only the
         * wake-up edge. STOP explicitly notifies this task for a final drain. */
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

static void ads_task(void *argument)
{
    ads_runtime_t *runtime = (ads_runtime_t *)argument;
    runtime->ads_task_core = xPortGetCoreID();
    __atomic_store_n(&runtime->health->ads_task_core, runtime->ads_task_core,
                     __ATOMIC_RELEASE);
    runtime->lifecycle = NOCTI_V4_ADS_STOPPED;
    __atomic_store_n(&runtime->gpio_isr_callback_core, -1, __ATOMIC_RELEASE);

    esp_err_t err = nocti_v4_time_init(runtime->timebase);
    if (err == ESP_OK) err = install_drdy_interrupt(runtime);
    if (err == ESP_OK) err = transport_init(runtime);
    if (err == ESP_OK) err = transport_acquire(&runtime->transport);
    if (err == ESP_OK) err = hardware_reset(runtime);
    if (err == ESP_OK) err = configure_8ksps(runtime);
    if (err == ESP_OK) err = validate_rate(runtime);
    transport_release(&runtime->transport);

    runtime->init_result = err;
    runtime->last_error = err;
    runtime->initialized = true;
    runtime->ready = err == ESP_OK && runtime->rate_valid;
    runtime->public_ads->interface_initialized = true;
    runtime->public_ads->hardware_initialized = err == ESP_OK;
    runtime->public_ads->ready = runtime->ready;
    if (err != ESP_OK) {
        runtime->lifecycle = NOCTI_V4_ADS_FAULTED;
        ESP_LOGE(TAG, "ADS init/rate validation failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG,
                 "ADS ready ID=%04X MODE=%04X CLOCK=%04X GAIN=%04X CFG=%04X rate=%lu SPS period=%lu us min=%lu max=%lu",
                 runtime->registers.id, runtime->registers.mode, runtime->registers.clock,
                 runtime->registers.gain, runtime->registers.cfg,
                 (unsigned long)runtime->measured_sps,
                 (unsigned long)runtime->validation_period_avg_us,
                 (unsigned long)runtime->validation_period_min_us,
                 (unsigned long)runtime->validation_period_max_us);
    }
    (void)xSemaphoreGive(runtime->init_done);

    for (;;) {
        /* DRDY's event ring is the record of work. The notification only wakes
         * this priority-20 task. A timed runtime wait services the documented
         * 5-ms DRDY-stall watchdog; STOPPED waits indefinitely for a command. */
        const TickType_t wait_ticks = __atomic_load_n(&runtime->capture_enabled,
                                                      __ATOMIC_ACQUIRE) ?
            pdMS_TO_TICKS(1u) : portMAX_DELAY;
        (void)ulTaskNotifyTake(pdTRUE, wait_ticks);

        ads_command_t command;
        while (xQueueReceive(runtime->command_queue, &command, 0u) == pdTRUE) {
            switch (command.type) {
                case ADS_COMMAND_START_DARK:
                    runtime->command_result = start_dark_internal(runtime);
                    break;
                case ADS_COMMAND_STOP_DARK:
                    runtime->command_result = stop_dark_internal(runtime);
                    break;
                case ADS_COMMAND_FRAME_DUMP:
                    runtime->command_result = capture_frame_dump_internal(
                        runtime, command.frame_count);
                    break;
                case ADS_COMMAND_CHANNEL_REGS:
                    runtime->command_result =
                        capture_channel_registers_internal(runtime);
                    break;
                case ADS_COMMAND_CH0_SELFTEST:
                    runtime->command_result =
                        capture_ch0_selftest_internal(runtime);
                    break;
                default:
                    runtime->command_result = ESP_ERR_INVALID_ARG;
                    break;
            }
            (void)xSemaphoreGive(runtime->command_done);
        }
        if (__atomic_load_n(&runtime->capture_enabled, __ATOMIC_ACQUIRE)) {
            process_runtime_events(runtime);
            check_drdy_stall(runtime);
        }
    }
}

static esp_err_t execute_command(nocti_v4_ads_t *ads, ads_command_type_t type,
                                 uint8_t frame_count)
{
    if (ads == NULL || ads->runtime == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    ads_runtime_t *runtime = (ads_runtime_t *)ads->runtime;
    if (xSemaphoreTake(runtime->command_lock,
                       pdMS_TO_TICKS(NOCTI_V4_ADS_CONTROL_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    (void)xSemaphoreTake(runtime->command_done, 0u);
    const ads_command_t command = {.type = type, .frame_count = frame_count};
    esp_err_t result = ESP_OK;
    if (xQueueSend(runtime->command_queue, &command,
                   pdMS_TO_TICKS(NOCTI_V4_ADS_CONTROL_TIMEOUT_MS)) != pdTRUE) {
        result = ESP_ERR_TIMEOUT;
    } else {
        xTaskNotifyGive(runtime->ads_task);
        if (xSemaphoreTake(runtime->command_done,
                           pdMS_TO_TICKS(NOCTI_V4_ADS_CONTROL_TIMEOUT_MS)) != pdTRUE) {
            result = ESP_ERR_TIMEOUT;
        } else {
            result = runtime->command_result;
        }
    }
    (void)xSemaphoreGive(runtime->command_lock);
    return result;
}

esp_err_t nocti_v4_ads_init(nocti_v4_ads_t *ads, nocti_v4_board_t *board,
                             nocti_v4_time_t *timebase, nocti_v4_health_t *health)
{
    if (ads == NULL || board == NULL || timebase == NULL || health == NULL ||
        !board->gpio_ready || s_runtime_claimed) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!decoder_self_test()) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    memset(ads, 0, sizeof(*ads));
    memset(&s_runtime, 0, sizeof(s_runtime));
    s_runtime_claimed = true;
    ads_runtime_t *runtime = &s_runtime;
    runtime->public_ads = ads;
    runtime->board = board;
    runtime->timebase = timebase;
    runtime->health = health;
    runtime->raw_stats_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    runtime->ads_task_core = -1;
    runtime->dark_task_core = -1;
    __atomic_store_n(&runtime->raw_consumer_mode,
                     NOCTI_V4_ADS_RAW_CONSUMER_DARK_DIAGNOSTIC,
                     __ATOMIC_RELEASE);
    runtime->gpio_isr_install_core = UINT32_MAX;
    __atomic_store_n(&runtime->gpio_isr_callback_core, -1, __ATOMIC_RELEASE);
    __atomic_store_n(&health->ads_task_core, -1, __ATOMIC_RELEASE);
    __atomic_store_n(&health->dark_consumer_task_core, -1, __ATOMIC_RELEASE);
    ads->runtime = runtime;

    runtime->init_done = xSemaphoreCreateBinaryStatic(&runtime->init_done_storage);
    runtime->command_done = xSemaphoreCreateBinaryStatic(&runtime->command_done_storage);
    runtime->command_lock = xSemaphoreCreateMutexStatic(&runtime->command_lock_storage);
    runtime->drain_done = xSemaphoreCreateBinaryStatic(&runtime->drain_done_storage);
    runtime->command_queue = xQueueCreateStatic(4u, sizeof(ads_command_t),
                                                 runtime->command_queue_buffer,
                                                 &runtime->command_queue_storage);
    if (runtime->init_done == NULL || runtime->command_done == NULL ||
        runtime->command_lock == NULL || runtime->drain_done == NULL ||
        runtime->command_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCore(dark_consumer_task, "nocti_v4_dark",
                                NOCTI_V4_STACK_DARK_DIAGNOSTIC_BYTES, runtime,
                                NOCTI_V4_PRIO_DARK_DIAGNOSTIC, &runtime->dark_task,
                                NOCTI_V4_CORE_DARK_DIAGNOSTIC) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    __atomic_store_n(&runtime->raw_consumer_task, runtime->dark_task,
                     __ATOMIC_RELEASE);
    if (xTaskCreatePinnedToCore(ads_task, "nocti_v4_ads",
                                NOCTI_V4_STACK_ADC_BYTES, runtime,
                                NOCTI_V4_PRIO_ADC, &runtime->ads_task,
                                NOCTI_V4_CORE_REALTIME) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(runtime->init_done,
                       pdMS_TO_TICKS(NOCTI_V4_ADS_INIT_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return runtime->init_result;
}

esp_err_t nocti_v4_ads_start_dark(nocti_v4_ads_t *ads)
{
    return execute_command(ads, ADS_COMMAND_START_DARK, 0u);
}

esp_err_t nocti_v4_ads_stop_dark(nocti_v4_ads_t *ads)
{
    return execute_command(ads, ADS_COMMAND_STOP_DARK, 0u);
}

esp_err_t nocti_v4_ads_capture_frame_dump(nocti_v4_ads_t *ads,
                                           uint8_t frame_count)
{
    if (frame_count == 0u || frame_count > NOCTI_V4_ADS_FRAME_DUMP_MAX_FRAMES) {
        return ESP_ERR_INVALID_ARG;
    }
    return execute_command(ads, ADS_COMMAND_FRAME_DUMP, frame_count);
}

esp_err_t nocti_v4_ads_capture_channel_registers(nocti_v4_ads_t *ads)
{
    return execute_command(ads, ADS_COMMAND_CHANNEL_REGS, 0u);
}

esp_err_t nocti_v4_ads_run_ch0_selftest(nocti_v4_ads_t *ads)
{
    return execute_command(ads, ADS_COMMAND_CH0_SELFTEST, 0u);
}

void nocti_v4_ads_print_frame_dump(const nocti_v4_ads_t *ads)
{
    if (ads == NULL || ads->runtime == NULL) {
        printf("ADC_FRAME_DUMP state=NOT_INITIALIZED\n");
        return;
    }
    const ads_runtime_t *runtime = (const ads_runtime_t *)ads->runtime;
    const nocti_v4_ads_frame_dump_snapshot_t *dump = &runtime->frame_dump;
    printf("ADC_FRAME_DUMP valid=%u requested=%u captured=%u result=%s"
           " layout=STATUS@0,CH0@3,CH1@6,CH2@9,CH3@12,CRC@15"
           " word_bits=24 frame_bytes=%u source=ADS_SPI_WIRE_BEFORE_RAW_RING\n",
           dump->valid ? 1u : 0u, dump->requested, dump->captured,
           esp_err_to_name(dump->result), NOCTI_V4_ADS_FRAME_BYTES);
    for (uint8_t index = 0u; index < dump->captured; ++index) {
        const nocti_v4_ads_frame_dump_record_t *record = &dump->frame[index];
        printf("ADC_FRAME_RAW index=%u drdy_sequence=%" PRIu32
               " drdy_us=%" PRIu64 " spi_us=%" PRIu32 " bytes=",
               index, record->drdy_sequence, record->drdy_timestamp_us,
               record->spi_duration_us);
        for (uint8_t byte = 0u; byte < NOCTI_V4_ADS_FRAME_BYTES; ++byte) {
            printf("%s%02X", byte == 0u ? "" : ",", record->raw[byte]);
        }
        printf("\n");
        printf("ADC_FRAME_WORD index=%u status_raw=0x%06" PRIX32
               " ch0_raw=0x%06" PRIX32 " ch1_raw=0x%06" PRIX32
               " ch2_raw=0x%06" PRIX32 " ch3_raw=0x%06" PRIX32
               " crc_raw=0x%06" PRIX32 "\n",
               index, record->raw_word[0], record->raw_word[1],
               record->raw_word[2], record->raw_word[3],
               record->raw_word[4], record->raw_word[5]);
        printf("ADC_FRAME_DECODE index=%u valid=%u status=0x%04X"
               " drdy_bits=0x%X drdy0_new_data=%u"
               " ch0=%" PRId32 " ch1=%" PRId32 " ch2=%" PRId32
               " ch3=%" PRId32 " crc_received=0x%04X"
               " crc_calculated=0x%04X crc_valid=%u status_valid=%u"
               " f_resync=%u\n",
               index, record->valid ? 1u : 0u, record->status,
               record->status & ADS_STATUS_DRDY_MASK,
               (record->status & ADS_STATUS_DRDY0_MASK) != 0u ? 1u : 0u,
               record->channel[0], record->channel[1], record->channel[2],
               record->channel[3], record->crc_received,
               record->crc_calculated, record->crc_valid ? 1u : 0u,
               record->status_valid ? 1u : 0u,
               record->resync_occurred ? 1u : 0u);
    }
}

void nocti_v4_ads_print_channel_registers(const nocti_v4_ads_t *ads)
{
    if (ads == NULL || ads->runtime == NULL) {
        printf("ADC_CHANNEL_REGS state=NOT_INITIALIZED\n");
        return;
    }
    const ads_runtime_t *runtime = (const ads_runtime_t *)ads->runtime;
    const nocti_v4_ads_channel_register_snapshot_t *snapshot =
        &runtime->channel_registers;
    printf("ADC_CHANNEL_REGS valid=%u result=%s register_stride=5"
           " normal_calibration_writes=0 gcal_unity_raw=0x%06" PRIX32 "\n",
           snapshot->valid ? 1u : 0u, esp_err_to_name(snapshot->result),
           (uint32_t)ADS_GCAL_UNITY_RAW);
    for (uint8_t channel = 0u; channel < NOCTI_V4_ADS_CHANNEL_COUNT; ++channel) {
        const uint16_t cfg = snapshot->channel_cfg[channel];
        uint16_t phase_raw = (uint16_t)((cfg & ADS_CH_CFG_PHASE_MASK) >> 6);
        const int16_t phase = (phase_raw & 0x0200u) != 0u ?
            (int16_t)(phase_raw | 0xFC00u) : (int16_t)phase_raw;
        const uint32_t ocal = calibration_raw24(snapshot->ocal_msb[channel],
                                                snapshot->ocal_lsb[channel]);
        const uint32_t gcal = calibration_raw24(snapshot->gcal_msb[channel],
                                                snapshot->gcal_lsb[channel]);
        const uint32_t gain_ppm = (uint32_t)(((uint64_t)gcal * 1000000u +
                                              ADS_GCAL_UNITY_RAW / 2u) /
                                             ADS_GCAL_UNITY_RAW);
        printf("ADC_CHANNEL_REG channel=%u cfg=0x%04X mux=%u phase=%d"
               " dcblock_disabled=%u ocal_msb=0x%04X ocal_lsb=0x%04X"
               " ocal_raw=0x%06" PRIX32 " ocal_signed=%" PRId32
               " gcal_msb=0x%04X gcal_lsb=0x%04X"
               " gcal_raw=0x%06" PRIX32 " gain_factor_ppm=%" PRIu32
               " unity=%u\n",
               channel, cfg, cfg & ADS_CH_CFG_MUX_MASK, phase,
               (cfg & ADS_CH_CFG_DCBLK_DISABLE) != 0u ? 1u : 0u,
               snapshot->ocal_msb[channel], snapshot->ocal_lsb[channel],
               ocal, sign_extend_24(ocal), snapshot->gcal_msb[channel],
               snapshot->gcal_lsb[channel], gcal, gain_ppm,
               gcal == ADS_GCAL_UNITY_RAW ? 1u : 0u);
    }
}

void nocti_v4_ads_print_ch0_selftest(const nocti_v4_ads_t *ads)
{
    if (ads == NULL || ads->runtime == NULL) {
        printf("ADC_CH0_SELFTEST state=NOT_INITIALIZED\n");
        return;
    }
    const ads_runtime_t *runtime = (const ads_runtime_t *)ads->runtime;
    const nocti_v4_ads_ch0_selftest_snapshot_t *snapshot =
        &runtime->ch0_selftest;
    const char *interpretation = !snapshot->valid ?
        "TEST_INCOMPLETE" :
        (snapshot->signal_nonzero ?
             "ADC_CH0_DIGITAL_CORE_PASS_EXTERNAL_INPUT_PATH_SUSPECT" :
             "INTERNAL_DC_STILL_ZERO_DEEPER_ADC_DIAGNOSIS_REQUIRED");
    printf("ADC_CH0_SELFTEST valid=%u result=%s samples=%" PRIu32 "/%" PRIu32
           " crc_valid=%" PRIu32 " status_valid=%" PRIu32
           " drdy0_new_data=%" PRIu32 " min=%" PRId32 " max=%" PRId32
           " mean=%" PRId64 " signal_nonzero=%u interpretation=%s\n",
           snapshot->valid ? 1u : 0u, esp_err_to_name(snapshot->result),
           snapshot->samples_captured, snapshot->samples_requested,
           snapshot->crc_valid_frames, snapshot->status_valid_frames,
           snapshot->drdy0_new_data_frames, snapshot->minimum,
           snapshot->maximum, snapshot->mean,
           snapshot->signal_nonzero ? 1u : 0u, interpretation);
    printf("ADC_CH0_SELFTEST_RESTORE original_cfg=0x%04X test_cfg=0x%04X"
           " restored_cfg=0x%04X restore_verified=%u restore_result=%s"
           " mux_test=POSITIVE_DC_0b10 only_register_written=CH0_CFG\n",
           snapshot->original_ch0_cfg, snapshot->test_ch0_cfg,
           snapshot->restored_ch0_cfg,
           snapshot->restore_verified ? 1u : 0u,
           esp_err_to_name(snapshot->restore_result));
    const uint32_t retained = snapshot->samples_captured <
                              NOCTI_V4_ADS_FRAME_DUMP_MAX_FRAMES ?
        snapshot->samples_captured : NOCTI_V4_ADS_FRAME_DUMP_MAX_FRAMES;
    for (uint32_t index = 0u; index < retained; ++index) {
        const nocti_v4_ads_frame_dump_record_t *record =
            &snapshot->frame[index];
        printf("ADC_CH0_SELFTEST_RAW index=%" PRIu32 " status_raw=0x%06" PRIX32
               " ch0_raw=0x%06" PRIX32 " ch0=%" PRId32
               " crc_valid=%u status_valid=%u drdy0_new_data=%u bytes=",
               index, record->raw_word[0], record->raw_word[1],
               record->channel[0], record->crc_valid ? 1u : 0u,
               record->status_valid ? 1u : 0u,
               (record->status & ADS_STATUS_DRDY0_MASK) != 0u ? 1u : 0u);
        for (uint8_t byte = 0u; byte < NOCTI_V4_ADS_FRAME_BYTES; ++byte) {
            printf("%s%02X", byte == 0u ? "" : ",", record->raw[byte]);
        }
        printf("\n");
    }
}

esp_err_t nocti_v4_ads_bind_raw_consumer(nocti_v4_ads_t *ads,
                                         TaskHandle_t optical_task)
{
    if (ads == NULL || ads->runtime == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    ads_runtime_t *runtime = (ads_runtime_t *)ads->runtime;
    if (__atomic_load_n(&runtime->lifecycle, __ATOMIC_ACQUIRE) !=
        NOCTI_V4_ADS_STOPPED) {
        return ESP_ERR_INVALID_STATE;
    }
    if (raw_depth(runtime) != 0u) {
        return ESP_ERR_INVALID_STATE;
    }
    const nocti_v4_ads_raw_consumer_mode_t mode = optical_task == NULL ?
        NOCTI_V4_ADS_RAW_CONSUMER_DARK_DIAGNOSTIC : NOCTI_V4_ADS_RAW_CONSUMER_OPTICAL;
    __atomic_store_n(&runtime->raw_consumer_mode, mode, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime->raw_consumer_task,
                     optical_task == NULL ? runtime->dark_task : optical_task,
                     __ATOMIC_RELEASE);
    return ESP_OK;
}

bool nocti_v4_ads_pop_raw_for_bound_consumer(nocti_v4_ads_t *ads,
                                             nocti_v4_raw_sample_t *out_sample)
{
    if (ads == NULL || ads->runtime == NULL || out_sample == NULL) {
        return false;
    }
    ads_runtime_t *runtime = (ads_runtime_t *)ads->runtime;
    if (__atomic_load_n(&runtime->raw_consumer_mode, __ATOMIC_ACQUIRE) !=
            NOCTI_V4_ADS_RAW_CONSUMER_OPTICAL ||
        __atomic_load_n(&runtime->raw_consumer_task, __ATOMIC_ACQUIRE) !=
            xTaskGetCurrentTaskHandle()) {
        return false;
    }
    if (!raw_pop(runtime, out_sample)) {
        return false;
    }
    counter_add(&runtime->health->raw_samples_consumed, 1u);
    return true;
}

bool nocti_v4_ads_bound_consumer_drain_requested(const nocti_v4_ads_t *ads)
{
    if (ads == NULL || ads->runtime == NULL) {
        return false;
    }
    return __atomic_load_n(&((const ads_runtime_t *)ads->runtime)->drain_requested,
                           __ATOMIC_ACQUIRE);
}

void nocti_v4_ads_bound_consumer_ack_drain(nocti_v4_ads_t *ads)
{
    if (ads == NULL || ads->runtime == NULL) {
        return;
    }
    ads_runtime_t *runtime = (ads_runtime_t *)ads->runtime;
    if (__atomic_load_n(&runtime->raw_consumer_mode, __ATOMIC_ACQUIRE) ==
            NOCTI_V4_ADS_RAW_CONSUMER_OPTICAL &&
        __atomic_load_n(&runtime->raw_consumer_task, __ATOMIC_ACQUIRE) ==
            xTaskGetCurrentTaskHandle() && raw_depth(runtime) == 0u &&
        __atomic_exchange_n(&runtime->drain_requested, false, __ATOMIC_ACQ_REL)) {
        (void)xSemaphoreGive(runtime->drain_done);
    }
}

bool nocti_v4_ads_is_active(const nocti_v4_ads_t *ads)
{
    if (ads == NULL || ads->runtime == NULL) {
        return false;
    }
    const nocti_v4_ads_lifecycle_t state =
        __atomic_load_n(&((ads_runtime_t *)ads->runtime)->lifecycle, __ATOMIC_ACQUIRE);
    return state == NOCTI_V4_ADS_STARTING_DARK || state == NOCTI_V4_ADS_RUNNING_DARK ||
           state == NOCTI_V4_ADS_STOPPING_DARK ||
           (state == NOCTI_V4_ADS_FAULTED &&
            __atomic_load_n(&((ads_runtime_t *)ads->runtime)->ready,
                            __ATOMIC_ACQUIRE));
}

bool nocti_v4_ads_ready(const nocti_v4_ads_t *ads)
{
    return ads != NULL && ads->ready;
}

esp_err_t nocti_v4_ads_reset_counters(nocti_v4_ads_t *ads)
{
    if (ads == NULL || ads->runtime == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    ads_runtime_t *runtime = (ads_runtime_t *)ads->runtime;
    if (runtime->lifecycle != NOCTI_V4_ADS_STOPPED) {
        return ESP_ERR_INVALID_STATE;
    }
    reset_acquisition_counters(runtime);
    counter_store(&runtime->health->dark_sessions_started, 0u);
    counter_store(&runtime->health->dark_sessions_stopped, 0u);
    counter_store(&runtime->health->dark_session_faults, 0u);
    return ESP_OK;
}

void nocti_v4_ads_sample_stack_watermarks(nocti_v4_ads_t *ads)
{
    if (ads == NULL || ads->runtime == NULL) {
        return;
    }
    ads_runtime_t *runtime = (ads_runtime_t *)ads->runtime;
    if (runtime->ads_task != NULL) {
        counter_store(&runtime->health->ads_task_stack_watermark_bytes,
                      uxTaskGetStackHighWaterMark(runtime->ads_task));
    }
    if (runtime->dark_task != NULL) {
        counter_store(&runtime->health->dark_consumer_task_stack_watermark_bytes,
                      uxTaskGetStackHighWaterMark(runtime->dark_task));
    }
}

void nocti_v4_ads_get_status(const nocti_v4_ads_t *ads,
                              nocti_v4_ads_status_snapshot_t *out_status)
{
    if (out_status == NULL) {
        return;
    }
    memset(out_status, 0, sizeof(*out_status));
    if (ads == NULL || ads->runtime == NULL) {
        return;
    }
    const ads_runtime_t *runtime = (const ads_runtime_t *)ads->runtime;
    out_status->interface_initialized = ads->interface_initialized;
    out_status->hardware_initialized = ads->hardware_initialized;
    out_status->ready = __atomic_load_n(&runtime->ready, __ATOMIC_ACQUIRE);
    out_status->rate_valid = __atomic_load_n(&runtime->rate_valid, __ATOMIC_ACQUIRE);
    out_status->session_integrity_fault = __atomic_load_n(
        &runtime->session_integrity_fault, __ATOMIC_ACQUIRE);
    out_status->lifecycle = __atomic_load_n(&runtime->lifecycle, __ATOMIC_ACQUIRE);
    out_status->session_epoch = __atomic_load_n(&runtime->session_epoch, __ATOMIC_ACQUIRE);
    out_status->configured_sps = NOCTI_V4_ADS_SAMPLE_RATE_HZ;
    out_status->measured_sps = runtime->measured_sps;
    out_status->validation_period_avg_us = runtime->validation_period_avg_us;
    out_status->validation_period_min_us = runtime->validation_period_min_us;
    out_status->validation_period_max_us = runtime->validation_period_max_us;
    out_status->validation_samples = runtime->validation_samples;
    out_status->raw_ring_depth = raw_depth(runtime);
    out_status->raw_ring_depth_max = __atomic_load_n(&runtime->raw_depth_max,
                                                      __ATOMIC_ACQUIRE);
    out_status->drdy_event_ring_depth = event_depth(runtime);
    out_status->drdy_event_ring_depth_max = __atomic_load_n(&runtime->event_depth_max,
                                                             __ATOMIC_ACQUIRE);
    out_status->sync_discard_remaining = runtime->sync_discard_remaining;
    out_status->expected_resync_remaining = runtime->expected_resync_remaining;
    out_status->ads_task_core = runtime->ads_task_core;
    out_status->dark_consumer_task_core = runtime->dark_task_core;
    out_status->raw_consumer_mode = __atomic_load_n(&runtime->raw_consumer_mode,
                                                     __ATOMIC_ACQUIRE);
    out_status->gpio_isr_install_core = (int32_t)runtime->gpio_isr_install_core;
    out_status->gpio_isr_callback_core = __atomic_load_n(&runtime->gpio_isr_callback_core,
                                                          __ATOMIC_ACQUIRE);
    out_status->last_error = runtime->last_error;
    out_status->drdy_isr_count = __atomic_load_n(&runtime->health->drdy_isr_count,
                                                  __ATOMIC_ACQUIRE);
    out_status->drdy_events_pushed = __atomic_load_n(&runtime->health->drdy_events_pushed,
                                                      __ATOMIC_ACQUIRE);
    out_status->drdy_events_popped = __atomic_load_n(&runtime->health->drdy_events_popped,
                                                      __ATOMIC_ACQUIRE);
    out_status->drdy_isr_timestamp_errors = __atomic_load_n(
        &runtime->health->drdy_isr_timestamp_errors, __ATOMIC_ACQUIRE);
    out_status->drdy_event_overflow = __atomic_load_n(
        &runtime->health->drdy_event_overflow, __ATOMIC_ACQUIRE);
    out_status->ads_frames_read_total = __atomic_load_n(
        &runtime->health->ads_frames_read_total, __ATOMIC_ACQUIRE);
    out_status->ads_frames_read_runtime = __atomic_load_n(
        &runtime->health->ads_frames_read_runtime, __ATOMIC_ACQUIRE);
    out_status->post_sync_frames_read = __atomic_load_n(
        &runtime->health->post_sync_frames_read, __ATOMIC_ACQUIRE);
    out_status->post_sync_frames_discarded = __atomic_load_n(
        &runtime->health->post_sync_frames_discarded, __ATOMIC_ACQUIRE);
    out_status->raw_samples_pushed = __atomic_load_n(&runtime->health->raw_samples_pushed,
                                                      __ATOMIC_ACQUIRE);
    out_status->raw_samples_consumed = __atomic_load_n(
        &runtime->health->raw_samples_consumed, __ATOMIC_ACQUIRE);
    out_status->adc_missed_sequence = __atomic_load_n(
        &runtime->health->adc_missed_sequence, __ATOMIC_ACQUIRE);
    out_status->adc_missed_backlog = __atomic_load_n(
        &runtime->health->adc_missed_backlog, __ATOMIC_ACQUIRE);
    out_status->adc_missed_other = __atomic_load_n(
        &runtime->health->adc_missed_other, __ATOMIC_ACQUIRE);
    out_status->last_drdy_sequence = __atomic_load_n(&runtime->last_drdy_sequence,
                                                      __ATOMIC_ACQUIRE);
    out_status->last_drdy_timestamp_us = load_last_drdy_timestamp(runtime);
    out_status->last_fault_code = __atomic_load_n(&runtime->last_fault_code,
                                                  __ATOMIC_ACQUIRE);
    out_status->last_fault_origin = __atomic_load_n(&runtime->last_fault_origin,
                                                    __ATOMIC_ACQUIRE);
    out_status->last_fault_error = __atomic_load_n(&runtime->last_fault_error,
                                                   __ATOMIC_ACQUIRE);
    out_status->runtime_period_count = runtime->runtime_period_count;
    memcpy(out_status->runtime_period_us, runtime->runtime_period_us,
           sizeof(out_status->runtime_period_us));
    out_status->drdy_period_lt_100_us = __atomic_load_n(
        &runtime->health->drdy_period_lt_100_us, __ATOMIC_ACQUIRE);
    out_status->drdy_period_100_to_150_us = __atomic_load_n(
        &runtime->health->drdy_period_100_to_150_us, __ATOMIC_ACQUIRE);
    out_status->drdy_period_150_to_250_us = __atomic_load_n(
        &runtime->health->drdy_period_150_to_250_us, __ATOMIC_ACQUIRE);
    out_status->drdy_period_gt_250_us = __atomic_load_n(
        &runtime->health->drdy_period_gt_250_us, __ATOMIC_ACQUIRE);
    out_status->spi_duration_gt_50_us = __atomic_load_n(
        &runtime->health->spi_duration_gt_50_us, __ATOMIC_ACQUIRE);
    out_status->spi_duration_gt_100_us = __atomic_load_n(
        &runtime->health->spi_duration_gt_100_us, __ATOMIC_ACQUIRE);
    out_status->spi_duration_gt_250_us = __atomic_load_n(
        &runtime->health->spi_duration_gt_250_us, __ATOMIC_ACQUIRE);
}

void nocti_v4_ads_get_registers(const nocti_v4_ads_t *ads,
                                 nocti_v4_ads_register_snapshot_t *out_regs)
{
    if (out_regs == NULL) {
        return;
    }
    memset(out_regs, 0, sizeof(*out_regs));
    if (ads != NULL && ads->runtime != NULL) {
        *out_regs = ((const ads_runtime_t *)ads->runtime)->registers;
    }
}

void nocti_v4_ads_get_raw_stats(const nocti_v4_ads_t *ads,
                                 nocti_v4_ads_raw_stats_snapshot_t *out_stats)
{
    if (out_stats == NULL) {
        return;
    }
    memset(out_stats, 0, sizeof(*out_stats));
    if (ads == NULL || ads->runtime == NULL) {
        return;
    }
    ads_runtime_t *runtime = (ads_runtime_t *)ads->runtime;
    portENTER_CRITICAL(&runtime->raw_stats_lock);
    *out_stats = runtime->raw_stats_published;
    portEXIT_CRITICAL(&runtime->raw_stats_lock);
}

const char *nocti_v4_ads_lifecycle_name(nocti_v4_ads_lifecycle_t state)
{
    switch (state) {
        case NOCTI_V4_ADS_STOPPED: return "STOPPED";
        case NOCTI_V4_ADS_STARTING_DARK: return "STARTING_DARK";
        case NOCTI_V4_ADS_RUNNING_DARK: return "RUNNING_DARK";
        case NOCTI_V4_ADS_STOPPING_DARK: return "STOPPING_DARK";
        case NOCTI_V4_ADS_FAULTED: return "FAULTED";
        default: return "UNKNOWN";
    }
}

const char *nocti_v4_ads_fault_code_name(nocti_v4_ads_fault_code_t code)
{
    switch (code) {
        case NOCTI_V4_ADS_FAULT_NONE: return "NONE";
        case NOCTI_V4_ADS_FAULT_START_BUS_ACQUIRE: return "START_BUS_ACQUIRE";
        case NOCTI_V4_ADS_FAULT_START_SYNC: return "START_SYNC";
        case NOCTI_V4_ADS_FAULT_START_INTERRUPT: return "START_INTERRUPT";
        case NOCTI_V4_ADS_FAULT_POST_SYNC_TIMEOUT: return "POST_SYNC_TIMEOUT";
        case NOCTI_V4_ADS_FAULT_ISR_TIMESTAMP: return "ISR_TIMESTAMP";
        case NOCTI_V4_ADS_FAULT_EVENT_RING_OVERFLOW: return "EVENT_RING_OVERFLOW";
        case NOCTI_V4_ADS_FAULT_EVENT_BACKLOG: return "EVENT_BACKLOG";
        case NOCTI_V4_ADS_FAULT_EVENT_EPOCH: return "EVENT_EPOCH";
        case NOCTI_V4_ADS_FAULT_SEQUENCE_GAP: return "SEQUENCE_GAP";
        case NOCTI_V4_ADS_FAULT_SPI_READ: return "SPI_READ";
        case NOCTI_V4_ADS_FAULT_CRC: return "CRC";
        case NOCTI_V4_ADS_FAULT_STATUS: return "STATUS";
        case NOCTI_V4_ADS_FAULT_F_RESYNC: return "F_RESYNC";
        case NOCTI_V4_ADS_FAULT_POST_SYNC_STATE: return "POST_SYNC_STATE";
        case NOCTI_V4_ADS_FAULT_RAW_RING_OVERFLOW: return "RAW_RING_OVERFLOW";
        case NOCTI_V4_ADS_FAULT_DRDY_STALL: return "DRDY_STALL";
        case NOCTI_V4_ADS_FAULT_STOP_DRAIN_TIMEOUT: return "STOP_DRAIN_TIMEOUT";
        default: return "UNKNOWN";
    }
}

const char *nocti_v4_ads_fault_origin_name(nocti_v4_ads_fault_origin_t origin)
{
    switch (origin) {
        case NOCTI_V4_ADS_FAULT_ORIGIN_NONE: return "NONE";
        case NOCTI_V4_ADS_FAULT_ORIGIN_START_DARK: return "START_DARK";
        case NOCTI_V4_ADS_FAULT_ORIGIN_DRDY_ISR: return "DRDY_ISR";
        case NOCTI_V4_ADS_FAULT_ORIGIN_PROCESS_EVENT: return "PROCESS_EVENT";
        case NOCTI_V4_ADS_FAULT_ORIGIN_RUNTIME_SERVICE: return "RUNTIME_SERVICE";
        case NOCTI_V4_ADS_FAULT_ORIGIN_DRDY_WATCHDOG: return "DRDY_WATCHDOG";
        case NOCTI_V4_ADS_FAULT_ORIGIN_STOP_DARK: return "STOP_DARK";
        default: return "UNKNOWN";
    }
}
