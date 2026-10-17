#include "nn2_board_v1.h"

#include "driver/spi_master.h"

const nn2_ads131_pins_t nn2_board_ads131_pins = {
    .spi_host = SPI2_HOST,
    .cs = 7,
    .sclk = 36,
    .mosi = 38,
    /* Placeholder pending Agent 04. Netlist: fNIRS_MISO = IO37. */
    .miso = 37,
    /* Placeholder pending Agent 04. Netlist: fNIRS_DRDY = IO35. */
    .drdy = 35,
    .sync_reset = 6,
};

const nn2_adc_config_t nn2_board_ads131_cfg = {
    .spi_hz = NN2_ADS131_SPI_HZ_MAX,
    /* Placeholders, not a locked OSR or data rate. */
    .osr = NN2_BOARD_ADS131_OSR_TBD35,
    .data_rate_hz = NN2_BOARD_ADS131_RATE_TBD35,
    /* CCITT is proposed. ANSI remains selectable. Pending Agent 04. */
    .crc_kind = NN2_ADC_CRC_CCITT,
    .ring_depth = 0,
};
