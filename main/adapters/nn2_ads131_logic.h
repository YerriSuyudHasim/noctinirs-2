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
typedef struct {
    void *ctx;
    nn2_status_t (*transfer)(void *, const uint8_t *, uint8_t *);
    nn2_status_t (*reset)(void *, bool high);
    nn2_status_t (*sync)(void *); /* Atomic 1us LOW pulse; must not become reset. */
    void (*delay_us)(void *, uint32_t);
    nn2_status_t (*wait_ready)(void *, uint32_t timeout_ms);
    uint64_t (*now_us)(void *);
} nn2_ads131_io_t;
typedef struct {
    nn2_ads131_io_t io;
    nn2_core_adc_record_t items[NN2_ADS131_CAPACITY];
    nn2_core_adc_observation_t observation;
    uint32_t head, count, quality, settling;
    uint64_t last_service_us;
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
