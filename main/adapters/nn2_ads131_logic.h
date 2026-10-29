/* Adapter-private pure logic; one owner context, not a public port handle.
 * No RTOS/SDK dependency. Host fake seam has identical call/lifetime rules. */
#ifndef NN2_ADS131_LOGIC_H
#define NN2_ADS131_LOGIC_H
#include "nn2_core_adc_record.h"
#define NN2_ADS131_BYTES 18u
#define NN2_ADS131_CAPACITY 128u
typedef struct {
    uint32_t clkin_hz, osr, rate_hz, spi_hz;
} nn2_ads131_config_t;
typedef struct { uint64_t time_us; uint32_t serial; } nn2_ads131_event_t;
typedef struct {
    void *ctx;
    nn2_status_t (*transfer)(void *, const uint8_t *, uint8_t *);
    nn2_status_t (*reset)(void *, bool high);
    nn2_status_t (*sync)(void *); /* Atomic 1us LOW pulse; must not become reset. */
    void (*delay_us)(void *, uint32_t);
    nn2_status_t (*wait_ready)(void *, uint32_t timeout_ms);
    uint64_t (*now_us)(void *);
    uint64_t (*drdy_latch)(void *, uint32_t *serial); /* Optional diagnostic seam. */
    /* Optional production event source. Atomically drains recorded ISR events;
     * returns queued + dropped event count, never a task notification value. */
    uint32_t (*take_events)(void *, nn2_ads131_event_t *, uint32_t *dropped);
} nn2_ads131_io_t;
/* Boot-lifetime first startup failure. Owned by the ADC task; STOP/reinit
 * cannot overwrite it. RX is unavailable on transport failure, never stale. */
typedef enum {
    NN2_ADS131_STAGE_NONE, NN2_ADS131_STAGE_RESET_LOW,
    NN2_ADS131_STAGE_RESET_HIGH, NN2_ADS131_STAGE_RREG,
    NN2_ADS131_STAGE_RREG_RESPONSE, NN2_ADS131_STAGE_WREG,
    NN2_ADS131_STAGE_WREG_RESPONSE, NN2_ADS131_STAGE_READY,
    NN2_ADS131_STAGE_SYNC
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
typedef struct {
    bool valid;
    nn2_core_adc_fault_t fault;
    uint32_t pending, dropped, expected_serial, event_serial, observed_serial;
    uint64_t edge_us, at_us, last_service_us;
    nn2_status_t cleanup_result;
} nn2_ads131_runtime_failure_t;
typedef struct {
    nn2_ads131_io_t io;
    nn2_core_adc_record_t items[NN2_ADS131_CAPACITY];
    nn2_core_adc_observation_t observation;
    uint32_t head, count, quality, settling;
    uint64_t last_service_us;
    uint32_t edge_serial;
    nn2_ads131_startup_failure_t startup_failure;
    nn2_ads131_stage_t startup_stage;
    uint32_t startup_transaction;
    uint16_t previous_command;
    nn2_ads131_runtime_failure_t runtime_failure;
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
nn2_status_t nn2_ads131_service(nn2_ads131_logic_t *dev, uint32_t notifications);
nn2_status_t nn2_ads131_pop(nn2_ads131_logic_t *dev, nn2_core_adc_record_t *out);
nn2_status_t nn2_ads131_fail(nn2_ads131_logic_t *dev, nn2_core_adc_fault_t reason);
#endif
