#ifndef NN2_ADAPTER_ADS131_H
#define NN2_ADAPTER_ADS131_H
#include "nn2_port_adc_stream.h"
#include "nn2_ads131_logic.h"
typedef struct {
    int reset, cs, drdy, sclk, miso, mosi, host;
    nn2_ads131_config_t config;
} nn2_ads131_board_t;
/* Composition only; once per boot, before any port use. Copies board config. */
nn2_status_t nn2_adapter_ads131_create(const nn2_ads131_board_t *board, nn2_port_adc_stream_t *out);
#endif
