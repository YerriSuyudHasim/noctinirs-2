/* Adapter-private pure logic; one owner context, not a public port handle.
 * No RTOS/SDK dependency. Host fake seam has identical call/lifetime rules. */
#ifndef NN2_ADS131_LOGIC_H
#define NN2_ADS131_LOGIC_H
#include "nn2_core_adc_record.h"
#define NN2_ADS131_BYTES 18u
#define NN2_ADS131_CAPACITY 128u
#define NN2_ADS131_TRACE_CAPACITY 8u
#define NN2_ADS131_RATE_SAMPLES 1024u
#define NN2_ADS131_LATENCY_CAPACITY 4u
typedef struct {
    uint32_t clkin_hz, osr, rate_hz, spi_hz;
} nn2_ads131_config_t;
typedef struct {
    uint64_t time_us; /* ISR entry, not the physical GPIO edge. */
    uint32_t serial, isr_publish_us; /* Entry to event publication. */
} nn2_ads131_event_t;
typedef struct {
    void *ctx;
    nn2_status_t (*transfer)(void *, const uint8_t *, uint8_t *);
    nn2_status_t (*reset)(void *, bool high);
    nn2_status_t (*sync)(void *); /* 5us LOW pulse; must not become reset. */
    void (*delay_us)(void *, uint32_t);
    nn2_status_t (*wait_ready)(void *, uint32_t timeout_ms);
    uint64_t (*now_us)(void *);
    uint64_t (*drdy_latch)(void *, uint32_t *serial); /* Optional diagnostic seam. */
    /* Optional production event source. Atomically pops the oldest recorded ISR event;
     * returns pre-pop queued + dropped count, never a notification value. */
    uint32_t (*take_events)(void *, nn2_ads131_event_t *, uint32_t *dropped);
    /* Optional production sample ring. Task-only, nonblocking; full is fatal. */
    nn2_status_t (*publish)(void *, const nn2_core_adc_record_t *, uint32_t *depth);
} nn2_ads131_io_t;
/* Boot-lifetime first startup failure. Owned by the ADC task; STOP/reinit
 * cannot overwrite it. RX is unavailable on transport failure, never stale. */
typedef enum {
    NN2_ADS131_STAGE_NONE, NN2_ADS131_STAGE_RESET_LOW,
    NN2_ADS131_STAGE_RESET_HIGH, NN2_ADS131_STAGE_RREG,
    NN2_ADS131_STAGE_RREG_RESPONSE, NN2_ADS131_STAGE_WREG,
    NN2_ADS131_STAGE_WREG_RESPONSE, NN2_ADS131_STAGE_READY,
    NN2_ADS131_STAGE_SYNC, NN2_ADS131_STAGE_RESET_ACK, NN2_ADS131_STAGE_UNLOCK
} nn2_ads131_stage_t;
typedef struct {
    bool valid, tx_valid, rx_valid, crc_match;
    nn2_ads131_stage_t stage;
    nn2_core_adc_fault_t fault;
    nn2_status_t result, cleanup_result;
    uint32_t transaction;
    uint16_t command, previous_command, crc_calculated, crc_received;
    uint8_t address, response_padding, crc_padding;
    uint8_t tx[NN2_ADS131_BYTES], rx[NN2_ADS131_BYTES];
} nn2_ads131_startup_failure_t;
/* Detail codes refine GAP without changing the public fault/status ABI. */
typedef enum {
    NN2_ADS131_RUNTIME_UNSPECIFIED,
    NN2_ADS131_RUNTIME_BACKLOG,
    NN2_ADS131_RUNTIME_SERVICE_GAP,
    NN2_ADS131_RUNTIME_EDGE_MISSING,
    NN2_ADS131_RUNTIME_SERIAL_BEFORE,
    NN2_ADS131_RUNTIME_EDGE_FUTURE,
    NN2_ADS131_RUNTIME_AGE_BEFORE,
    NN2_ADS131_RUNTIME_SERIAL_AFTER,
    NN2_ADS131_RUNTIME_AGE_AFTER, NN2_ADS131_RUNTIME_RATE, NN2_ADS131_RUNTIME_RAW_OVERFLOW
} nn2_ads131_runtime_detail_t;
typedef struct {
    bool valid;
    /* Use former alignment padding; do not increase the device memory budget. */
    uint8_t detail;
    uint16_t spi_elapsed_us; /* Saturated at UINT16_MAX. */
    nn2_core_adc_fault_t fault;
    uint32_t pending, dropped, expected_serial, event_serial, observed_serial;
    uint64_t edge_us, at_us, last_service_us;
    nn2_status_t cleanup_result;
} nn2_ads131_runtime_failure_t;
/* Task-owned bounded history; never written or printed by the ISR. */
typedef struct {
    uint64_t edge_us, spi_begin_us, spi_end_us;
    uint64_t owner_resume_us, service_entry_us, event_pop_us;
    uint64_t driver_start_us, driver_started_us;
    uint32_t serial, period_us, crc_us, pending, isr_publish_us;
    uint16_t status;
    uint8_t phase; /* 0=before transfer, 1=transport returned, 2=CRC checked. */
} nn2_ads131_trace_t;
typedef struct {
    uint64_t first_us, last_us;
    uint32_t hz, min_period_us, max_period_us, period_gaps;
    uint16_t samples;
    bool active, valid;
} nn2_ads131_rate_t;
enum { NN2_ADS131_UNKNOWN_PERIOD = 1u, NN2_ADS131_UNKNOWN_FRESHNESS = 2u };
/* First nonfatal continuity uncertainty per START, retained through STOP.
 * Task-owned, captured only once; logs are deferred until conversion stops. */
typedef struct {
    bool valid, previous_valid;
    uint8_t reasons;
    uint32_t session, observed_serial;
    uint64_t checked_us;
    nn2_ads131_trace_t previous, current;
} nn2_ads131_unknown_t;
enum {
    NN2_ADS131_LATENCY_PERIOD = 1u, NN2_ADS131_LATENCY_FRESHNESS = 2u,
    NN2_ADS131_LATENCY_START_LATE = 4u, NN2_ADS131_LATENCY_SERVICE_WORK = 8u,
    NN2_ADS131_LATENCY_TRANSPORT = 16u
};
typedef struct {
    uint8_t reasons;
    bool previous_valid;
    nn2_ads131_trace_t previous, current;
} nn2_ads131_latency_event_t;
typedef struct {
    nn2_ads131_io_t io;
    nn2_core_adc_record_t items[NN2_ADS131_CAPACITY];
    nn2_core_adc_observation_t observation;
    uint32_t head, count, quality;
    uint16_t settling, expected_resync; /* Three discard frames, one expected F_RESYNC. */
    uint64_t last_service_us;
    uint32_t edge_serial;
    nn2_ads131_startup_failure_t startup_failure;
    nn2_ads131_stage_t startup_stage;
    uint32_t startup_transaction;
    uint16_t previous_command;
    nn2_ads131_runtime_failure_t runtime_failure;
    nn2_ads131_trace_t trace[NN2_ADS131_TRACE_CAPACITY];
    uint32_t trace_head;
    uint64_t previous_edge_us;
    nn2_ads131_rate_t rate;
    nn2_ads131_unknown_t first_unknown;
    uint64_t owner_resume_us;
    uint32_t latency_seen, latency_count;
    nn2_ads131_latency_event_t latency[NN2_ADS131_LATENCY_CAPACITY];
} nn2_ads131_logic_t;
uint16_t nn2_ads131_crc(const uint8_t *data, size_t size);
int32_t nn2_ads131_sext24(uint32_t value);
nn2_status_t nn2_ads131_parse(const uint8_t raw[NN2_ADS131_BYTES], uint64_t time,
                            nn2_core_adc_record_t *out);
nn2_status_t nn2_ads131_validate(const nn2_ads131_config_t *cfg);
void nn2_ads131_logic_bind(nn2_ads131_logic_t *dev, nn2_ads131_io_t io);
nn2_status_t nn2_ads131_initialize(nn2_ads131_logic_t *dev, const nn2_ads131_config_t *cfg);
nn2_status_t nn2_ads131_start(nn2_ads131_logic_t *dev);
nn2_status_t nn2_ads131_stop(nn2_ads131_logic_t *dev);
void nn2_ads131_rate_begin(nn2_ads131_logic_t *dev);
nn2_status_t nn2_ads131_service(nn2_ads131_logic_t *dev, uint32_t notifications);
nn2_status_t nn2_ads131_pop(nn2_ads131_logic_t *dev, nn2_core_adc_record_t *out);
nn2_status_t nn2_ads131_fail(nn2_ads131_logic_t *dev, nn2_core_adc_fault_t reason);
#endif
