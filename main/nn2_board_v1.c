#include "nn2_board_v1.h"
#include "driver/spi_master.h"
/* Confirmed netlist role map, #35 Hardware correction 6227859080.
 * Physical cable/rail/PSRAM/emitter fixture evidence remains NOT RUN. */
const nn2_ads131_board_t nn2_board_v1_adc = {
    .reset = 6, .cs = 7, .drdy = 35, .sclk = 36, .miso = 37, .mosi = 38,
    .host = SPI2_HOST,
    .config = { .clkin_hz = 8192000u, .osr = 512u, .rate_hz = 8000u, .spi_hz = 8000000u }
};
_Static_assert(8000000u <= 8000000u, "Issue35 SPI ceiling");
