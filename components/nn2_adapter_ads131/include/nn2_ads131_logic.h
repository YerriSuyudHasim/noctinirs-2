#ifndef NN2_ADS131_LOGIC_H
#define NN2_ADS131_LOGIC_H

#include <stdbool.h>
#include <stdint.h>

#include "nn2_frame_ring.h"
#include "nn2_port_adc.h"
#include "nn2_status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NN2_ADS131_FRAME_BYTES 18u
#define NN2_ADS131_SPI_HZ_MAX 8000000u
#define NN2_ADS131_RESET_LOW_US 250u
#define NN2_ADS131_TREGACQ_US 5u
#define NN2_ADS131_GPIO_INDEX_MAX 48

_Static_assert(NN2_ADS131_SPI_HZ_MAX == 8000000u, "SPI cap is 8 MHz");
_Static_assert(NN2_ADS131_SPI_HZ_MAX <= 8000000u, "SPI cap");
/* 250 us is 2048 tCLKIN at 8.192 MHz (SBAS890D tw(RSL)). */
_Static_assert(NN2_ADS131_RESET_LOW_US * 8192u >= 2048u * 1000u,
               "reset low time covers tw(RSL)");

typedef enum {
    NN2_ADS131_SIG_CS = 0,
    NN2_ADS131_SIG_SCLK,
    NN2_ADS131_SIG_MOSI,
    NN2_ADS131_SIG_MISO,
    NN2_ADS131_SIG_DRDY,
    NN2_ADS131_SIG_SYNC
} nn2_ads131_sig_t;

typedef enum {
    NN2_PIN_INPUT = 0,
    NN2_PIN_OUTPUT = 1
} nn2_pin_dir_t;

/* GPIO numbers come from the composition root. This struct has no
   defaults. spi_host is the SPI peripheral index, not a GPIO. */
typedef struct {
    int spi_host;
    int cs;
    int sclk;
    int mosi;
    int miso;
    int drdy;
    int sync_reset;
} nn2_ads131_pins_t;

typedef struct {
    nn2_adc_config_t cfg;
    bool cfg_valid;
    bool safe_applied;
    /* Written from the composition task, read from nn2_ads131. */
    volatile bool running;
    uint32_t crc_bad;
    uint32_t spi_errors;
    uint32_t drdy_timeouts;
    uint32_t frames_produced;
    nn2_frame_ring_t ring;
    nn2_adc_raw_frame_t *storage;
    uint16_t storage_capacity;
} nn2_ads131_logic_t;

nn2_pin_dir_t nn2_ads131_pin_dir(nn2_ads131_sig_t signal);

nn2_status_t nn2_ads131_validate_pins(const nn2_ads131_pins_t *pins);

nn2_status_t nn2_ads131_validate_config(const nn2_adc_config_t *cfg,
                                        uint16_t storage_capacity,
                                        uint16_t *depth_out);

/* ceil(data_rate * 50 ms) + 25%, recorded for the backlog note.
   The live depth stays at the default until the rate is fixed. */
uint32_t nn2_ads131_ring_model_depth(uint32_t data_rate_hz);

/* Bounded notify timeout of about two DRDY periods, in RTOS ticks.
   Returns 0 when an argument is zero. Never returns an unbounded wait.
   Tick resolution can stretch the wait to one tick. */
uint32_t nn2_ads131_notify_timeout_ticks(uint32_t data_rate_hz,
                                         uint32_t tick_hz);

int32_t nn2_ads131_sext24(uint32_t word);

nn2_status_t nn2_ads131_parse_frame(const uint8_t raw[NN2_ADS131_FRAME_BYTES],
                                    nn2_adc_crc_kind_t crc_kind,
                                    uint64_t timestamp_us,
                                    nn2_adc_raw_frame_t *out);

nn2_status_t nn2_ads131_logic_init(nn2_ads131_logic_t *logic,
                                   const nn2_adc_config_t *cfg,
                                   nn2_adc_raw_frame_t *storage,
                                   uint16_t storage_capacity);

nn2_status_t nn2_ads131_logic_safe_state(nn2_ads131_logic_t *logic);

nn2_status_t nn2_ads131_logic_begin_run(nn2_ads131_logic_t *logic);

nn2_status_t nn2_ads131_logic_allow_task(const nn2_ads131_logic_t *logic);

/* One timeout while running counts one fault and returns NN2_ERR_FAULT.
   A timeout while held in reset is not a DRDY fault. */
nn2_status_t nn2_ads131_logic_on_timeout(nn2_ads131_logic_t *logic);

nn2_status_t nn2_ads131_logic_on_spi_error(nn2_ads131_logic_t *logic);

/* Parses, checks CRC, and pushes once. Does not read the bus again. */
nn2_status_t nn2_ads131_logic_on_frame(nn2_ads131_logic_t *logic,
                                       const uint8_t raw[NN2_ADS131_FRAME_BYTES],
                                       uint64_t timestamp_us);

nn2_status_t nn2_ads131_logic_pop(nn2_ads131_logic_t *logic,
                                  nn2_adc_raw_frame_t *out);

nn2_status_t nn2_ads131_logic_counters(const nn2_ads131_logic_t *logic,
                                       nn2_adc_counters_t *out);

#ifdef __cplusplus
}
#endif

#endif /* NN2_ADS131_LOGIC_H */
